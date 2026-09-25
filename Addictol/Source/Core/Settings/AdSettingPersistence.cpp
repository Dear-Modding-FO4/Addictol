#include <Core/Settings/AdSettingPersistence.h>

#include <Core/AdConfigValidation.h>

#include <toml11/single_include/toml.hpp>

#include <Windows.h>
#undef ERROR

#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		inline constexpr std::string_view kManagedHelpBegin{
			"# >>> Addictol managed help:"
		};
		inline constexpr std::string_view kManagedHelpEnd{
			"# <<< Addictol managed help"
		};
		inline constexpr std::string_view kUtf8Bom{ "\xEF\xBB\xBF" };

		std::atomic_uint64_t g_temporaryFileSequence{ 0 };
		std::string g_settingsStorePath;

		[[nodiscard]] bool ValueMatchesType(
			SettingValueType a_type,
			const SettingValue& a_value) noexcept
		{
			switch (a_type)
			{
			case SettingValueType::kBoolean:
				return std::holds_alternative<bool>(a_value);
			case SettingValueType::kFloat32:
				return std::holds_alternative<double>(a_value);
			case SettingValueType::kInt32:
			{
				const auto* value = std::get_if<int64_t>(&a_value);
				return value &&
					*value >= (std::numeric_limits<int32_t>::min)() &&
					*value <= (std::numeric_limits<int32_t>::max)();
			}
			case SettingValueType::kUInt32:
			{
				const auto* value = std::get_if<uint64_t>(&a_value);
				return value &&
					*value <= (std::numeric_limits<uint32_t>::max)();
			}
			case SettingValueType::kString:
				return std::holds_alternative<std::string>(a_value);
			}
			return false;
		}

		[[nodiscard]] toml::ordered_value ToTomlValue(const SettingValue& a_value)
		{
			return std::visit(
				[](const auto& a_item) -> toml::ordered_value {
					using T = std::remove_cvref_t<decltype(a_item)>;
					if constexpr (std::is_same_v<T, uint64_t>)
						return toml::ordered_value{ static_cast<int64_t>(a_item) };
					else
						return toml::ordered_value{ a_item };
				},
				a_value);
		}

		[[nodiscard]] std::string Trim(std::string_view a_line)
		{
			const auto first = a_line.find_first_not_of(" \t\r");
			if (first == std::string_view::npos)
				return {};
			const auto last = a_line.find_last_not_of(" \t\r");
			return std::string{ a_line.substr(first, last - first + 1) };
		}

		[[nodiscard]] std::vector<std::string> SplitLines(
			std::string_view a_text)
		{
			std::vector<std::string> lines;
			size_t start = 0;
			while (start < a_text.size())
			{
				const auto end = a_text.find('\n', start);
				const auto count = end == std::string_view::npos ?
					a_text.size() - start :
					end - start;
				auto line = std::string{ a_text.substr(start, count) };
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(std::move(line));
				if (end == std::string_view::npos)
					break;
				start = end + 1;
			}
			return lines;
		}

		[[nodiscard]] std::string JoinLines(
			const std::vector<std::string>& a_lines)
		{
			std::string output;
			for (const auto& line : a_lines)
			{
				output += line;
				output.push_back('\n');
			}
			return output;
		}

		void MarkMultilineStringLines(
			const toml::ordered_value& a_value,
			std::vector<bool>& a_lines)
		{
			if (a_value.is_string())
			{
				const auto format = a_value.as_string_fmt().fmt;
				if (format == toml::string_format::multiline_basic ||
					format == toml::string_format::multiline_literal)
				{
					const auto location = a_value.location();
					if (location.is_ok())
					{
						for (auto line = location.first_line_number();
							line <= location.last_line_number() &&
							line <= a_lines.size();
							++line)
							a_lines[line - 1] = true;
					}
				}
				return;
			}
			if (a_value.is_array())
			{
				for (const auto& item : a_value.as_array())
					MarkMultilineStringLines(item, a_lines);
				return;
			}
			if (a_value.is_table())
			{
				for (const auto& [key, item] : a_value.as_table())
				{
					(void)key;
					MarkMultilineStringLines(item, a_lines);
				}
			}
		}

		[[nodiscard]] std::vector<bool> MultilineStringLines(
			const toml::ordered_value& a_root,
			size_t a_lineCount)
		{
			std::vector<bool> lines(a_lineCount, false);
			MarkMultilineStringLines(a_root, lines);
			return lines;
		}

		[[nodiscard]] std::vector<std::string> StripManagedHelp(
			std::string_view a_text,
			const toml::ordered_value& a_root)
		{
			auto lines = SplitLines(a_text);
			const auto multilineStrings =
				MultilineStringLines(a_root, lines.size());
			std::vector<std::string> output;
			bool inManagedHelp = false;
			for (size_t index = 0; index < lines.size(); ++index)
			{
				auto& line = lines[index];
				if (multilineStrings[index])
				{
					output.push_back(std::move(line));
					continue;
				}

				const auto trimmed = Trim(line);
				if (!inManagedHelp &&
					trimmed.starts_with(kManagedHelpBegin))
				{
					inManagedHelp = true;
					continue;
				}
				if (inManagedHelp)
				{
					if (trimmed == kManagedHelpEnd)
					{
						inManagedHelp = false;
						continue;
					}
					if (!trimmed.empty() && !trimmed.starts_with('#'))
						output.push_back(std::move(line));
					continue;
				}
				output.push_back(std::move(line));
			}
			return output;
		}

		[[nodiscard]] std::string FormatDefault(
			const SettingEntry& a_setting)
		{
			return toml::format(ToTomlValue(a_setting.DefaultValue()));
		}

		[[nodiscard]] bool IsIgnorableEmptyOwnedSection(
			std::string_view a_key,
			const toml::ordered_value& a_value)
		{
			return a_value.is_table() &&
				a_value.as_table().empty() &&
				SettingRegistry::GetSingleton().ContainsSection(a_key);
		}

		[[nodiscard]] bool SemanticallyEqual(
			const toml::ordered_value& a_left,
			const toml::ordered_value& a_right,
			bool a_root = true)
		{
			if (a_left.type() != a_right.type())
				return false;
			switch (a_left.type())
			{
			case toml::value_t::boolean:
				return a_left.as_boolean() == a_right.as_boolean();
			case toml::value_t::integer:
				return a_left.as_integer() == a_right.as_integer();
			case toml::value_t::floating:
				return a_left.as_floating() == a_right.as_floating() ||
					(std::isnan(a_left.as_floating()) &&
						std::isnan(a_right.as_floating()));
			case toml::value_t::string:
				return a_left.as_string() == a_right.as_string();
			case toml::value_t::offset_datetime:
				return a_left.as_offset_datetime() ==
					a_right.as_offset_datetime();
			case toml::value_t::local_datetime:
				return a_left.as_local_datetime() ==
					a_right.as_local_datetime();
			case toml::value_t::local_date:
				return a_left.as_local_date() == a_right.as_local_date();
			case toml::value_t::local_time:
				return a_left.as_local_time() == a_right.as_local_time();
			case toml::value_t::array:
			{
				const auto& left = a_left.as_array();
				const auto& right = a_right.as_array();
				if (left.size() != right.size())
					return false;
				for (size_t index = 0; index < left.size(); ++index)
				{
					if (!SemanticallyEqual(left[index], right[index], false))
						return false;
				}
				return true;
			}
			case toml::value_t::table:
			{
				const auto& left = a_left.as_table();
				const auto& right = a_right.as_table();
				for (const auto& [key, value] : left)
				{
					const auto position = right.find(key);
					if (position == right.end())
					{
						if (a_root &&
							IsIgnorableEmptyOwnedSection(key, value))
							continue;
						return false;
					}
					if (!SemanticallyEqual(value, position->second, false))
						return false;
				}
				for (const auto& [key, value] : right)
				{
					if (left.contains(key))
						continue;
					if (a_root &&
						IsIgnorableEmptyOwnedSection(key, value))
						continue;
					return false;
				}
				return true;
			}
			case toml::value_t::empty:
				return true;
			}
			return false;
		}

		[[nodiscard]] bool ParseDocument(
			std::string_view a_text,
			toml::ordered_value& a_output,
			std::string& a_error)
		{
			auto parsed = toml::try_parse_str<toml::ordered_type_config>(
				std::string{ a_text });
			if (!parsed.is_ok())
			{
				a_error = "existing settings TOML could not be parsed";
				return false;
			}
			a_output = std::move(parsed).unwrap();
			if (!a_output.is_table())
			{
				a_error = "existing settings root is not a TOML table";
				return false;
			}
			return true;
		}

		[[nodiscard]] bool ValidateSerializedDocument(
			const toml::ordered_value& a_expected,
			std::string_view a_output,
			std::string& a_error)
		{
			auto parsed = toml::try_parse_str<toml::ordered_type_config>(
				std::string{ a_output });
			if (!parsed.is_ok() || !parsed.unwrap().is_table())
			{
				a_error = "generated settings TOML could not be parsed";
				return false;
			}
			if (!SemanticallyEqual(a_expected, parsed.unwrap()))
			{
				a_error = "generated settings TOML changed unrelated data";
				return false;
			}
			return true;
		}

		// Inline children have no comment slots; retain their notes on the
		// containing assignment instead of letting the formatter discard them.
		void MakeInline(
			toml::ordered_value& a_value,
			toml::ordered_value::comment_type& a_comments)
		{
			const auto& comments = a_value.comments();
			a_comments.insert(a_comments.end(), comments.begin(), comments.end());
			a_value.comments().clear();
			if (a_value.is_table())
			{
				a_value.as_table_fmt().fmt = toml::table_format::oneline;
				for (auto& [key, value] : a_value.as_table())
				{
					(void)key;
					MakeInline(value, a_comments);
				}
			}
			else if (a_value.is_array())
			{
				a_value.as_array_fmt().fmt = toml::array_format::oneline;
				for (auto& value : a_value.as_array())
					MakeInline(value, a_comments);
			}
		}

		[[nodiscard]] std::string FormatAssignment(
			const std::string& a_key,
			toml::ordered_value a_value,
			bool a_preserveComments = true)
		{
			toml::ordered_value::comment_type comments;
			MakeInline(a_value, comments);
			if (a_preserveComments)
				a_value.comments() = std::move(comments);
			// A single-entry root lets toml11 quote keys and retain attached comments.
			toml::ordered_value root{ toml::ordered_table{} };
			root.as_table().emplace(a_key, std::move(a_value));
			return toml::format(root);
		}

		void AppendChunk(std::string& a_output, std::string a_chunk, bool a_separate = true)
		{
			while (!a_chunk.empty() && a_chunk.back() == '\n')
				a_chunk.pop_back();
			if (a_chunk.empty())
				return;
			if (a_separate)
				a_output += '\n';
			a_output += a_chunk;
			a_output += '\n';
		}

		[[nodiscard]] bool RenderDocument(
			const toml::ordered_value& a_document,
			std::string_view a_source,
			std::string& a_output,
			std::string& a_error)
		{
			const auto& registry = SettingRegistry::GetSingleton();
			const auto& root = a_document.as_table();
			a_output = a_source.starts_with(kUtf8Bom) ?
				std::string{ kUtf8Bom } : std::string{};
			a_output += "# Addictol settings. Uncomment a line to change it; edits apply on the next launch.\n";

			toml::ordered_value rootValues{ toml::ordered_table{} };
			for (const auto& [key, value] : root)
			{
				if (!registry.ContainsSection(key) && !value.is_table())
					rootValues.as_table().emplace(key, value);
			}
			AppendChunk(a_output, toml::format(rootValues));

			std::string_view currentSection;
			const toml::ordered_table* table = nullptr;
			bool sectionStart = false;
			for (const auto* setting : registry.Settings())
			{
				const auto section = std::string{ setting->Section() };
				if (currentSection != section)
				{
					currentSection = setting->Section();
					const auto position = root.find(section);
					table = nullptr;
					if (position != root.end())
					{
						if (!position->second.is_table())
						{
							a_error = "owned setting section is not a TOML table: " + section;
							return false;
						}
						table = &position->second.as_table();
					}
					toml::ordered_value header{ toml::ordered_table{} };
					header.as_table_fmt().fmt = toml::table_format::multiline;
					AppendChunk(a_output, toml::format(section, header));
					sectionStart = true;
					if (table)
					{
						for (const auto& [key, value] : *table)
						{
							if (!registry.Find(section, key))
							{
								AppendChunk(a_output, FormatAssignment(key, value), !sectionStart);
								sectionStart = false;
							}
						}
					}
				}

				const auto key = std::string{ setting->Key() };
				auto entry = "# " + std::string{ setting->Description() } + "\n";
				if (table && table->contains(key))
					entry += FormatAssignment(key, table->at(key), false);
				else
					entry += "# " + key + " = " + FormatDefault(*setting);
				AppendChunk(a_output, std::move(entry), !sectionStart);
				sectionStart = false;
			}

			for (const auto& [key, value] : root)
			{
				if (registry.ContainsSection(key) || !value.is_table())
					continue;
				auto tableValue = value;
				// Root inline/dotted tables must become explicit headers here:
				// otherwise their assignments would belong to the last owned section.
				tableValue.as_table_fmt().fmt = toml::table_format::multiline;
				AppendChunk(a_output, toml::format(key, tableValue));
			}
			if (!ValidateSerializedDocument(a_document, a_output, a_error))
				return false;
			a_error.clear();
			return true;
		}

		[[nodiscard]] bool ParseUnmanagedDocument(
			std::string_view a_source,
			toml::ordered_value& a_document,
			std::string& a_error)
		{
			if (a_source.starts_with(kUtf8Bom))
				a_source.remove_prefix(kUtf8Bom.size());
			if (!ParseDocument(a_source, a_document, a_error))
				return false;
			const auto unmanaged = JoinLines(StripManagedHelp(a_source, a_document));
			return ParseDocument(unmanaged, a_document, a_error);
		}

		[[nodiscard]] bool UpdateOwnedSettings(
			toml::ordered_value& a_output,
			std::span<const SettingValueSnapshot> a_settings,
			std::string& a_error)
		{
			auto& root = a_output.as_table();
			for (const auto& item : a_settings)
			{
				const auto section = std::string{ item.setting->Section() };
				auto sectionPosition = root.find(section);
				if (sectionPosition != root.end() &&
					!sectionPosition->second.is_table())
				{
					a_error =
						"owned setting section is not a TOML table: " +
						section;
					return false;
				}

				if (item.value == item.setting->DefaultValue())
				{
					if (sectionPosition == root.end())
						continue;
					auto& table = sectionPosition->second.as_table();
					const auto valuePosition = table.find(
						std::string{ item.setting->Key() });
					if (valuePosition == table.end())
						continue;
					table.erase(valuePosition);
					continue;
				}

				if (sectionPosition == root.end())
				{
					root.emplace(
						section,
						toml::ordered_value{ toml::ordered_table{} });
					sectionPosition = root.find(section);
				}
				auto& table = sectionPosition->second.as_table();
				const auto key = std::string{ item.setting->Key() };
				const auto valuePosition = table.find(key);
				if (valuePosition == table.end())
				{
					table.emplace(key, ToTomlValue(item.value));
					continue;
				}
				valuePosition->second = ToTomlValue(item.value);
			}
			return true;
		}

		[[nodiscard]] std::filesystem::path TemporaryPath(
			const std::filesystem::path& a_target)
		{
			auto name = a_target.filename().wstring();
			name += L".tmp.";
			name += std::to_wstring(GetCurrentProcessId());
			name += L".";
			name += std::to_wstring(
				g_temporaryFileSequence.fetch_add(
					1,
					std::memory_order_relaxed));
			return a_target.parent_path() / name;
		}

		[[nodiscard]] bool WriteAtomically(
			const std::filesystem::path& a_target,
			std::string_view a_contents,
			std::string& a_error)
		{
			std::error_code filesystemError;
			if (!a_target.parent_path().empty())
			{
				std::filesystem::create_directories(
					a_target.parent_path(),
					filesystemError);
				if (filesystemError)
				{
					a_error = "could not create the settings directory";
					return false;
				}
			}

			const auto temporary = TemporaryPath(a_target);
			{
				std::ofstream file{
					temporary,
					std::ios::binary | std::ios::trunc
				};
				if (!file)
				{
					a_error = "could not create a temporary settings file";
					return false;
				}
				file.write(
					a_contents.data(),
					static_cast<std::streamsize>(a_contents.size()));
				file.flush();
				if (!file)
				{
					file.close();
					std::filesystem::remove(temporary, filesystemError);
					a_error = "could not write the temporary settings file";
					return false;
				}
			}

			if (!MoveFileExW(
					temporary.c_str(),
					a_target.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED))
			{
				std::filesystem::remove(temporary, filesystemError);
				a_error = "could not replace the settings file";
				return false;
			}
			return true;
		}

		[[nodiscard]] bool ReadExistingFile(
			const std::filesystem::path& a_path,
			std::string& a_existing,
			bool& a_exists,
			std::string& a_error)
		{
			std::error_code filesystemError;
			a_exists = std::filesystem::exists(a_path, filesystemError);
			if (filesystemError)
			{
				a_error = "could not inspect the settings file";
				return false;
			}
			if (!a_exists)
			{
				a_existing.clear();
				return true;
			}

			std::ifstream file{ a_path, std::ios::binary };
			if (!file)
			{
				a_error = "could not read the settings file";
				return false;
			}
			a_existing.assign(
				std::istreambuf_iterator<char>{ file },
				std::istreambuf_iterator<char>{});
			if (file.bad())
			{
				a_error = "could not read the complete settings file";
				return false;
			}
			return true;
		}

		[[nodiscard]] bool WriteIfChanged(
			const std::filesystem::path& a_path,
			std::string_view a_existing,
			std::string_view a_output,
			bool a_exists,
			bool* a_changed,
			std::string& a_error)
		{
			const auto changed = !a_exists || a_existing != a_output;
			if (a_changed)
				*a_changed = false;
			if (!changed)
			{
				a_error.clear();
				return true;
			}
			if (!WriteAtomically(a_path, a_output, a_error))
				return false;
			if (a_changed)
				*a_changed = true;
			return true;
		}
	}

	bool BuildSettingsDocumentToml(
		std::string_view a_existingToml,
		std::string& a_output,
		std::string& a_error) noexcept
	{
		try
		{
			toml::ordered_value document;
			if (!ParseUnmanagedDocument(a_existingToml, document, a_error))
				return false;
			return RenderDocument(document, a_existingToml, a_output, a_error);
		}
		catch (const std::exception& error)
		{
			a_error = error.what();
			return false;
		}
		catch (...)
		{
			a_error = "unknown error while documenting settings";
			return false;
		}
	}

	bool BuildSettingsOverrideToml(
		std::string_view a_existingToml,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_output,
		std::string& a_error) noexcept
	{
		try
		{
			for (const auto& item : a_settings)
			{
				if (!item.setting ||
					!ValueMatchesType(item.setting->Type(), item.value))
				{
					a_error = "settings snapshot contains an invalid value";
					return false;
				}
			}

			toml::ordered_value output;
			if (!ParseUnmanagedDocument(a_existingToml, output, a_error))
				return false;
			if (!UpdateOwnedSettings(output, a_settings, a_error))
				return false;
			return RenderDocument(output, a_existingToml, a_output, a_error);
		}
		catch (const std::exception& error)
		{
			a_error = error.what();
			return false;
		}
		catch (...)
		{
			a_error = "unknown error while formatting settings";
			return false;
		}
	}

	bool WriteSettingsOverrideFile(
		const std::filesystem::path& a_path,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_error,
		bool* a_changed) noexcept
	{
		try
		{
			std::string existing;
			bool exists = false;
			if (!ReadExistingFile(
					a_path,
					existing,
					exists,
					a_error))
				return false;

			std::string output;
			if (!BuildSettingsOverrideToml(
					existing,
					a_settings,
					output,
					a_error))
				return false;
			return WriteIfChanged(
				a_path,
				existing,
				output,
				exists,
				a_changed,
				a_error);
		}
		catch (const std::exception& error)
		{
			a_error = error.what();
			return false;
		}
		catch (...)
		{
			a_error = "unknown error while saving settings";
			return false;
		}
	}

	bool RefreshSettingsDocument(
		const std::filesystem::path& a_path,
		std::string& a_error,
		bool* a_changed) noexcept
	{
		try
		{
			std::string existing;
			bool exists = false;
			if (!ReadExistingFile(
					a_path,
					existing,
					exists,
					a_error))
				return false;

			std::string output;
			if (!BuildSettingsDocumentToml(existing, output, a_error))
				return false;
			return WriteIfChanged(
				a_path,
				existing,
				output,
				exists,
				a_changed,
				a_error);
		}
		catch (const std::exception& error)
		{
			a_error = error.what();
			return false;
		}
		catch (...)
		{
			a_error = "unknown error while refreshing settings documentation";
			return false;
		}
	}

	std::filesystem::path ResolveSettingsPath(
		const std::filesystem::path& a_directory) noexcept
	{
		const auto custom = a_directory / kAddictolCustomSettingsFileName;
		std::error_code error;
		if (std::filesystem::exists(custom, error) && !error)
			return custom;
		return a_directory / kAddictolSettingsFileName;
	}

	std::filesystem::path ResolveSettingsPath() noexcept
	{
		return ResolveSettingsPath(std::filesystem::path{ kAddictolSettingsDirectory });
	}

	void InitializeSettings() noexcept
	{
		InitializeSettings(ResolveSettingsPath());
	}

	void InitializeSettings(const std::filesystem::path& a_path) noexcept
	{
		g_settingsStorePath = a_path.string();
		if (a_path.filename() == kAddictolCustomSettingsFileName)
			REX::INFO("Settings: using \"{}\""sv, g_settingsStorePath);

		std::string documentationError;
		if (!RefreshSettingsDocument(
				a_path,
				documentationError))
		{
			REX::ERROR(
				"Settings: could not create or refresh \"{}\": {}"sv,
				a_path.string(),
				documentationError);
		}

		const auto config = REX::FTomlSettingStore::GetSingleton();
		config->Init("", g_settingsStorePath.c_str());
		config->Load();
		ValidateConfigKeys(g_settingsStorePath.c_str());
	}

	SettingsRepository& SettingsRepository::GetSingleton() noexcept
	{
		static SettingsRepository singleton{
			g_settingsStorePath.empty() ?
				ResolveSettingsPath() : std::filesystem::path{ g_settingsStorePath }
		};
		return singleton;
	}

	SettingsRepository::SettingsRepository(std::filesystem::path a_path) :
		m_path(std::move(a_path))
	{
		const auto settings = SettingRegistry::GetSingleton().Settings();
		m_committed.reserve(settings.size());
		for (const auto* setting : settings)
			m_committed.push_back({ setting, setting->Value() });
	}

	std::vector<SettingValueSnapshot> SettingsRepository::Snapshot() const
	{
		const std::scoped_lock lock{ m_mutex };
		return m_committed;
	}

	SettingsApplyResult SettingsRepository::Apply(
		std::span<const SettingValueSnapshot> a_settings) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (a_settings.size() != m_committed.size())
			return { false, 0, "settings snapshot size does not match the registry" };

		std::vector<size_t> changed;
		changed.reserve(a_settings.size());
		for (size_t index = 0; index < a_settings.size(); ++index)
		{
			if (a_settings[index].setting != m_committed[index].setting ||
				!a_settings[index].setting ||
				!ValueMatchesType(
					a_settings[index].setting->Type(),
					a_settings[index].value))
				return { false, 0, "settings snapshot does not match the registry" };
			if (a_settings[index].value != m_committed[index].value)
				changed.push_back(index);
		}

		std::string error;
		if (!WriteSettingsOverrideFile(
				m_path,
				a_settings,
				error))
			return { false, 0, std::move(error) };

		for (const auto index : changed)
		{
			const auto& item = a_settings[index];
			if (item.setting->ApplyTiming() == SettingApplyTiming::kImmediate &&
				!item.setting->SetValue(item.value))
				return { false, 0, "an immediate setting rejected its value" };
			m_committed[index].value = item.value;
		}
		return { true, changed.size(), {} };
	}
}
