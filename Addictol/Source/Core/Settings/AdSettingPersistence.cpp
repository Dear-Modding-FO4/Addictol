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
#include <map>
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

		[[nodiscard]] toml::value ToTomlValue(const SettingValue& a_value)
		{
			return std::visit(
				[](const auto& a_item) -> toml::value {
					using T = std::remove_cvref_t<decltype(a_item)>;
					if constexpr (std::is_same_v<T, uint64_t>)
						return toml::value{ static_cast<int64_t>(a_item) };
					else
						return toml::value{ a_item };
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
			const toml::value& a_value,
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
			const toml::value& a_root,
			size_t a_lineCount)
		{
			std::vector<bool> lines(a_lineCount, false);
			MarkMultilineStringLines(a_root, lines);
			return lines;
		}

		[[nodiscard]] std::vector<std::string> StripManagedHelp(
			std::string_view a_text,
			const toml::value& a_root)
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

		[[nodiscard]] std::vector<std::string> BuildManagedHelp(
			std::string_view a_section,
			std::span<const SettingEntry* const> a_settings,
			bool a_qualifiedAssignments)
		{
			std::vector<std::string> lines;
			lines.push_back(
				std::string{ kManagedHelpBegin } + " [" +
				std::string{ a_section } + "]");
			lines.push_back(
				"# Generated from the C++ registry. Edit active assignments, not these comments.");
			lines.push_back(
				"# Put personal notes outside this block; uncomment an assignment to override its factory default.");
			for (const auto* setting : a_settings)
			{
				if (setting->Section() != a_section)
					continue;
				const auto key = a_qualifiedAssignments ?
					std::string{ a_section } + "." +
						std::string{ setting->Key() } :
					std::string{ setting->Key() };
				lines.emplace_back("#");
				lines.push_back("# " + std::string{ setting->Description() });
				lines.push_back(
					"# " + key + " = " +
					FormatDefault(*setting));
			}
			lines.push_back(std::string{ kManagedHelpEnd });
			return lines;
		}

		void AppendManagedHelp(
			std::vector<std::string>& a_output,
			std::string_view a_section,
			std::span<const SettingEntry* const> a_settings,
			bool a_qualifiedAssignments)
		{
			auto managed = BuildManagedHelp(
				a_section,
				a_settings,
				a_qualifiedAssignments);
			a_output.insert(
				a_output.end(),
				std::make_move_iterator(managed.begin()),
				std::make_move_iterator(managed.end()));
		}

		[[nodiscard]] std::string AddManagedHelp(
			std::string_view a_text,
			const toml::value& a_root)
		{
			const auto settings = SettingRegistry::GetSingleton().Settings();
			std::map<std::string, std::vector<const SettingEntry*>> sections;
			for (const auto* setting : settings)
				sections[std::string{ setting->Section() }].push_back(setting);

			auto lines = SplitLines(a_text);
			std::map<size_t, std::vector<std::string>> insertions;
			std::vector<std::string> newSections;
			std::vector<std::string> qualifiedHelp;
			const auto& root = a_root.as_table();
			for (const auto& [section, entries] : sections)
			{
				(void)entries;
				const auto position = root.find(section);
				if (position != root.end() &&
					position->second.is_table() &&
					position->second.as_table_fmt().fmt ==
						toml::table_format::multiline)
				{
					const auto location = position->second.location();
					if (location.is_ok() &&
						location.last_line_number() <= lines.size())
					{
						AppendManagedHelp(
							insertions[location.last_line_number()],
							section,
							settings,
							false);
						continue;
					}
				}

				if (position == root.end())
				{
					if (!newSections.empty() &&
						!newSections.back().empty())
						newSections.emplace_back();
					newSections.push_back("[" + section + "]");
					AppendManagedHelp(
						newSections,
						section,
						settings,
						false);
				}
				else
				{
					if (!qualifiedHelp.empty() &&
						!qualifiedHelp.back().empty())
						qualifiedHelp.emplace_back();
					AppendManagedHelp(
						qualifiedHelp,
						section,
						settings,
						true);
				}
			}

			std::vector<std::string> output;
			for (size_t index = 0; index < lines.size(); ++index)
			{
				output.push_back(std::move(lines[index]));
				const auto insertion = insertions.find(index + 1);
				if (insertion != insertions.end())
				{
					output.insert(
						output.end(),
						insertion->second.begin(),
						insertion->second.end());
				}
			}

			const auto appendTrailing =
				[&output](std::vector<std::string>& a_trailing) {
					if (a_trailing.empty())
						return;
					while (!output.empty() && output.back().empty())
						output.pop_back();
					if (!output.empty())
						output.emplace_back();
					output.insert(
						output.end(),
						std::make_move_iterator(a_trailing.begin()),
						std::make_move_iterator(a_trailing.end()));
				};
			appendTrailing(newSections);
			appendTrailing(qualifiedHelp);
			return JoinLines(output);
		}

		void CollectInlineComments(
			const toml::value& a_value,
			const std::vector<std::string>& a_lines,
			std::map<std::pair<size_t, size_t>, std::string>& a_comments)
		{
			const auto location = a_value.location();
			if (location.is_ok() &&
				location.last_line_number() != 0 &&
				location.last_line_number() <= a_lines.size() &&
				!a_value.comments().empty())
			{
				const auto lineIndex =
					location.last_line_number() - 1;
				const auto& line = a_lines[lineIndex];
				const auto searchStart = (std::min)(
					location.last_column_number() - 1,
					line.size());
				const auto commentPosition =
					line.find('#', searchStart);
				if (commentPosition != std::string::npos)
				{
					const auto comment = Trim(
						std::string_view{ line }.substr(
							commentPosition));
					const auto owned = std::ranges::find_if(
						a_value.comments(),
						[&](const auto& a_item) {
							return Trim(a_item) == comment;
						});
					if (owned != a_value.comments().end())
					{
						a_comments.emplace(
							std::pair{ lineIndex, commentPosition },
							comment);
					}
				}
			}

			if (a_value.is_array())
			{
				for (const auto& item : a_value.as_array())
					CollectInlineComments(item, a_lines, a_comments);
			}
			else if (a_value.is_table())
			{
				for (const auto& [key, item] : a_value.as_table())
				{
					(void)key;
					CollectInlineComments(item, a_lines, a_comments);
				}
			}
		}

		[[nodiscard]] std::vector<std::string> CollectAllComments(
			std::string_view a_text,
			const toml::value& a_root)
		{
			const auto lines = SplitLines(a_text);
			const auto multilineStrings =
				MultilineStringLines(a_root, lines.size());
			std::map<std::pair<size_t, size_t>, std::string> comments;
			for (size_t index = 0; index < lines.size(); ++index)
			{
				if (multilineStrings[index])
					continue;
				auto commentPosition =
					lines[index].find_first_not_of(" \t\r");
				if (index == 0 &&
					commentPosition == 0 &&
					lines[index].starts_with(kUtf8Bom))
				{
					commentPosition =
						lines[index].find_first_not_of(
							" \t\r",
							kUtf8Bom.size());
				}
				if (commentPosition != std::string::npos &&
					lines[index][commentPosition] == '#')
				{
					comments.emplace(
						std::pair{ index, commentPosition },
						Trim(
							std::string_view{ lines[index] }.substr(
								commentPosition)));
				}
			}
			CollectInlineComments(a_root, lines, comments);

			std::vector<std::string> output;
			output.reserve(comments.size());
			for (auto& [position, comment] : comments)
			{
				(void)position;
				output.push_back(std::move(comment));
			}
			return output;
		}

		void AppendMissingComments(
			std::string& a_output,
			const std::vector<std::string>& a_expected)
		{
			const auto parsed = toml::try_parse_str(a_output);
			if (!parsed.is_ok())
				return;
			std::map<std::string, size_t> present;
			for (const auto& comment :
				CollectAllComments(a_output, parsed.unwrap()))
				++present[comment];

			std::vector<std::string> missing;
			for (const auto& comment : a_expected)
			{
				auto& count = present[comment];
				if (count)
					--count;
				else
					missing.push_back(comment);
			}
			if (missing.empty())
				return;

			if (!a_output.empty() && a_output.back() != '\n')
				a_output.push_back('\n');
			if (!a_output.empty())
				a_output.push_back('\n');
			for (const auto& comment : missing)
			{
				a_output += comment;
				a_output.push_back('\n');
			}
		}

		[[nodiscard]] bool NormalizeOwnedSectionTables(
			toml::value& a_root)
		{
			bool changed = false;
			auto& root = a_root.as_table();
			for (const auto* setting :
				SettingRegistry::GetSingleton().Settings())
			{
				const auto position = root.find(
					std::string{ setting->Section() });
				if (position == root.end() ||
					!position->second.is_table() ||
					position->second.as_table_fmt().fmt ==
						toml::table_format::multiline)
					continue;
				position->second.as_table_fmt().fmt =
					toml::table_format::multiline;
				changed = true;
			}
			return changed;
		}

		[[nodiscard]] bool IsIgnorableEmptyOwnedSection(
			std::string_view a_key,
			const toml::value& a_value)
		{
			return a_value.is_table() &&
				a_value.as_table().empty() &&
				SettingRegistry::GetSingleton().ContainsSection(a_key);
		}

		[[nodiscard]] bool SemanticallyEqual(
			const toml::value& a_left,
			const toml::value& a_right,
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
			toml::value& a_output,
			std::string& a_error)
		{
			auto parsed = toml::try_parse_str(std::string{ a_text });
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
			const toml::value& a_expected,
			std::string_view a_output,
			std::string& a_error)
		{
			auto parsed = toml::try_parse_str(std::string{ a_output });
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

		[[nodiscard]] bool FormatPreservingComments(
			const toml::value& a_document,
			std::string_view a_source,
			const std::vector<std::string>& a_comments,
			std::string& a_output,
			std::string& a_error)
		{
			a_output = toml::format(a_document);
			if (!a_output.empty() && a_output.back() != '\n')
				a_output.push_back('\n');
			if (a_source.starts_with(kUtf8Bom) &&
				!a_output.starts_with(kUtf8Bom))
				a_output.insert(0, kUtf8Bom);
			AppendMissingComments(a_output, a_comments);
			return ValidateSerializedDocument(
				a_document,
				a_output,
				a_error);
		}

		void PreserveRemovedComments(
			toml::value& a_section,
			const toml::value& a_value)
		{
			auto& destination = a_section.comments();
			const auto& source = a_value.comments();
			destination.insert(destination.end(), source.begin(), source.end());
		}

		[[nodiscard]] bool UpdateOwnedSettings(
			toml::value& a_output,
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
					PreserveRemovedComments(
						sectionPosition->second,
						valuePosition->second);
					table.erase(valuePosition);
					continue;
				}

				if (sectionPosition == root.end())
				{
					sectionPosition = root.emplace(
						section,
						toml::value{ toml::table{} }).first;
				}
				auto& table = sectionPosition->second.as_table();
				const auto key = std::string{ item.setting->Key() };
				const auto valuePosition = table.find(key);
				if (valuePosition == table.end())
				{
					table.emplace(key, ToTomlValue(item.value));
					continue;
				}
				auto comments = valuePosition->second.comments();
				valuePosition->second = ToTomlValue(item.value);
				valuePosition->second.comments() = std::move(comments);
			}
			return true;
		}

		void RemoveEmptyTables(toml::value& a_output)
		{
			auto& root = a_output.as_table();
			for (auto position = root.begin(); position != root.end();)
			{
				if (position->second.is_table() &&
					position->second.as_table().empty() &&
					position->second.comments().empty() &&
					SettingRegistry::GetSingleton().ContainsSection(
						position->first))
					position = root.erase(position);
				else
					++position;
			}
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
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
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
			toml::value existing;
			if (!ParseDocument(a_existingToml, existing, a_error))
				return false;
			const auto unmanaged = JoinLines(
				StripManagedHelp(a_existingToml, existing));
			toml::value parsedUnmanaged;
			if (!ParseDocument(unmanaged, parsedUnmanaged, a_error))
				return false;
			const auto expected = parsedUnmanaged;
			std::string normalized;
			std::string_view helpSource = unmanaged;
			if (NormalizeOwnedSectionTables(parsedUnmanaged))
			{
				const auto comments =
					CollectAllComments(unmanaged, parsedUnmanaged);
				if (!FormatPreservingComments(
						parsedUnmanaged,
						unmanaged,
						comments,
						normalized,
						a_error))
					return false;
				if (!ParseDocument(
						normalized,
						parsedUnmanaged,
						a_error))
					return false;
				helpSource = normalized;
			}
			a_output = AddManagedHelp(helpSource, parsedUnmanaged);
			if (!ValidateSerializedDocument(
					expected,
					a_output,
					a_error))
				return false;
			a_error.clear();
			return true;
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

			toml::value existing;
			if (!ParseDocument(a_existingToml, existing, a_error))
				return false;
			const auto unmanaged = JoinLines(
				StripManagedHelp(a_existingToml, existing));
			toml::value output;
			if (!ParseDocument(unmanaged, output, a_error))
				return false;
			const auto comments = CollectAllComments(unmanaged, output);
			if (!UpdateOwnedSettings(output, a_settings, a_error))
				return false;

			RemoveEmptyTables(output);
			(void)NormalizeOwnedSectionTables(output);
			std::string formatted;
			if (!FormatPreservingComments(
					output,
					unmanaged,
					comments,
					formatted,
					a_error))
				return false;

			toml::value parsedFormatted;
			if (!ParseDocument(formatted, parsedFormatted, a_error))
				return false;
			a_output = AddManagedHelp(formatted, parsedFormatted);
			if (!ValidateSerializedDocument(output, a_output, a_error))
				return false;
			a_error.clear();
			return true;
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

	void InitializeSettings() noexcept
	{
		InitializeSettings(std::filesystem::path{ kAddictolSettingsPath });
	}

	void InitializeSettings(const std::filesystem::path& a_path) noexcept
	{
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

		g_settingsStorePath = a_path.string();
		const auto config = REX::FTomlSettingStore::GetSingleton();
		config->Init("", g_settingsStorePath.c_str());
		config->Load();
		ValidateConfigKeys(g_settingsStorePath.c_str());
	}

	SettingsRepository& SettingsRepository::GetSingleton() noexcept
	{
		static SettingsRepository singleton{
			std::filesystem::path{ kAddictolSettingsPath }
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
