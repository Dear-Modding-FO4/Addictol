#include <Core/Settings/AdSettingPersistence.h>

#include <toml11/single_include/toml.hpp>

#include <Windows.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

namespace Addictol
{
	namespace
	{
		std::atomic_uint64_t g_temporaryFileSequence{ 0 };

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

		[[nodiscard]] bool RemoveOwnedSettings(
			toml::value& a_output,
			std::span<const SettingValueSnapshot> a_settings,
			std::string& a_error)
		{
			auto& root = a_output.as_table();
			for (const auto& item : a_settings)
			{
				const auto section = std::string{ item.setting->Section() };
				const auto sectionPosition = root.find(section);
				if (sectionPosition == root.end())
					continue;
				if (!sectionPosition->second.is_table())
				{
					a_error =
						"owned setting section is not a TOML table: " +
						section;
					return false;
				}
				sectionPosition->second.as_table().erase(
					std::string{ item.setting->Key() });
			}
			return true;
		}

		void RemoveEmptyTables(toml::value& a_output)
		{
			auto& root = a_output.as_table();
			for (auto position = root.begin(); position != root.end();)
			{
				if (position->second.is_table() &&
					position->second.as_table().empty())
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
					a_error = "could not create the custom settings directory";
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
					a_error = "could not create a temporary custom settings file";
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
					a_error = "could not write the temporary custom settings file";
					return false;
				}
			}

			if (!MoveFileExW(
					temporary.c_str(),
					a_target.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				std::filesystem::remove(temporary, filesystemError);
				a_error = "could not replace the custom settings file";
				return false;
			}
			return true;
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
			toml::value output{ toml::table{} };
			if (!a_existingToml.empty())
			{
				auto parsed = toml::try_parse_str(
					std::string{ a_existingToml });
				if (!parsed.is_ok())
				{
					a_error = "existing custom settings TOML could not be parsed";
					return false;
				}
				output = std::move(parsed).unwrap();
			}
			if (!output.is_table())
			{
				a_error = "existing custom settings root is not a TOML table";
				return false;
			}
			for (const auto& item : a_settings)
			{
				if (!item.setting ||
					!ValueMatchesType(item.setting->Type(), item.value))
				{
					a_error = "settings snapshot contains an invalid value";
					return false;
				}
			}
			if (!RemoveOwnedSettings(output, a_settings, a_error))
				return false;

			for (const auto& item : a_settings)
			{
				if (item.value == item.setting->DefaultValue())
					continue;

				const auto section = std::string{ item.setting->Section() };
				auto& root = output.as_table();
				auto position = root.find(section);
				if (position == root.end())
				{
					position = root.emplace(
						section,
						toml::value{ toml::table{} }).first;
				}
				if (!position->second.is_table())
				{
					a_error =
						"owned setting section is not a TOML table: " +
						section;
					return false;
				}
				position->second.as_table().insert_or_assign(
					std::string{ item.setting->Key() },
					ToTomlValue(item.value));
			}

			RemoveEmptyTables(output);
			a_output = toml::format(output);
			if (!a_output.empty() && a_output.back() != '\n')
				a_output.push_back('\n');
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
			a_error = "unknown error while formatting custom settings";
			return false;
		}
	}

	bool WriteSettingsOverrideFile(
		const std::filesystem::path& a_path,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_error) noexcept
	{
		try
		{
			std::string existing;
			std::error_code filesystemError;
			if (std::filesystem::exists(a_path, filesystemError))
			{
				if (filesystemError)
				{
					a_error = "could not inspect the custom settings file";
					return false;
				}
				std::ifstream file{ a_path, std::ios::binary };
				if (!file)
				{
					a_error = "could not read the custom settings file";
					return false;
				}
				existing.assign(
					std::istreambuf_iterator<char>{ file },
					std::istreambuf_iterator<char>{});
				if (!file.eof())
				{
					a_error = "could not read the complete custom settings file";
					return false;
				}
			}
			else if (filesystemError)
			{
				a_error = "could not inspect the custom settings file";
				return false;
			}

			std::string output;
			if (!BuildSettingsOverrideToml(
					existing,
					a_settings,
					output,
					a_error))
				return false;
			return WriteAtomically(a_path, output, a_error);
		}
		catch (const std::exception& error)
		{
			a_error = error.what();
			return false;
		}
		catch (...)
		{
			a_error = "unknown error while saving custom settings";
			return false;
		}
	}

	SettingsRepository& SettingsRepository::GetSingleton() noexcept
	{
		static SettingsRepository singleton;
		return singleton;
	}

	SettingsRepository::SettingsRepository()
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
		if (changed.empty())
			return { true, 0, {} };

		std::string error;
		if (!WriteSettingsOverrideFile(
				std::filesystem::path{ kAddictolCustomSettingsPath },
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
