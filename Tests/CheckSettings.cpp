#include "Harness.h"

#include <Core/AdConfigValidation.h>
#include <Core/Settings/AdSetting.h>

#include <toml11/single_include/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <tuple>

namespace
{
	using SettingKey = std::pair<std::string, std::string>;

	[[nodiscard]] std::filesystem::path ShippedConfigPath()
	{
		return std::filesystem::path{ __FILE__ }.parent_path().parent_path() /
			".Build/F4SE/Plugins/Addictol.toml";
	}

	[[nodiscard]] std::set<SettingKey> ReadShippedKeys()
	{
		const auto result = toml::try_parse(ShippedConfigPath().string());
		vmm_tests::require(result.is_ok(), "shipped Addictol.toml could not be parsed");
		const auto& data = result.unwrap();
		vmm_tests::require(data.is_table(), "shipped Addictol.toml is not a table");

		std::set<SettingKey> keys;
		for (const auto& [sectionName, sectionValue] : data.as_table())
		{
			vmm_tests::require(
				sectionValue.is_table(),
				"shipped Addictol.toml contains a non-table section");
			for (const auto& [keyName, keyValue] : sectionValue.as_table())
			{
				(void)keyValue;
				keys.emplace(sectionName, keyName);
			}
		}
		return keys;
	}

	[[nodiscard]] bool MatchesType(
		Addictol::SettingValueType a_type,
		const Addictol::SettingValue& a_value)
	{
		switch (a_type)
		{
		case Addictol::SettingValueType::kBoolean:
			return std::holds_alternative<bool>(a_value);
		case Addictol::SettingValueType::kFloat32:
			return std::holds_alternative<double>(a_value);
		case Addictol::SettingValueType::kInt32:
			return std::holds_alternative<int64_t>(a_value);
		case Addictol::SettingValueType::kUInt32:
			return std::holds_alternative<uint64_t>(a_value);
		case Addictol::SettingValueType::kString:
			return std::holds_alternative<std::string>(a_value);
		}
		return false;
	}
}

namespace vmm_tests
{
	void run_setting_registry_checks(Runner& runner)
	{
		runner.test("setting registry and shipped config contain the same keys", [] {
			const auto shipped = ReadShippedKeys();
			std::set<SettingKey> registered;
			for (const auto* setting : Addictol::SettingRegistry::GetSingleton().Settings())
			{
				require(setting != nullptr, "registry contains a null setting");
				registered.emplace(setting->Section(), setting->Key());
			}
			require(
				registered == shipped,
				"registered settings and shipped Addictol.toml keys differ");
		});

		runner.test("setting registry has unique keys and complete metadata", [] {
			std::set<SettingKey> unique;
			for (const auto* setting : Addictol::SettingRegistry::GetSingleton().Settings())
			{
				require(
					unique.emplace(setting->Section(), setting->Key()).second,
					"registry contains a duplicate section and key");
				require(!setting->Description().empty(), "registered setting has no description");
				const auto timing = setting->ApplyTiming();
				require(
					timing == Addictol::SettingApplyTiming::kImmediate ||
						timing == Addictol::SettingApplyTiming::kNextLaunch,
					"registered setting has no explicit apply timing");
			}
		});

		runner.test("setting registry enumeration is stable and deterministic", [] {
			const auto first = Addictol::SettingRegistry::GetSingleton().Settings();
			const auto second = Addictol::SettingRegistry::GetSingleton().Settings();
			require(first.data() == second.data(), "registry enumeration storage changed");
			require(first.size() == second.size(), "registry enumeration size changed");
			for (size_t index = 0; index < first.size(); ++index)
			{
				require(first[index] == second[index], "registry enumeration order changed");
				if (index == 0)
					continue;
				const auto previous = std::tuple{
					first[index - 1]->Section(),
					first[index - 1]->Key()
				};
				const auto current = std::tuple{
					first[index]->Section(),
					first[index]->Key()
				};
				require(previous < current, "registry enumeration is not sorted");
			}
		});

		runner.test("config validation follows the setting registry", [] {
			for (const auto* setting : Addictol::SettingRegistry::GetSingleton().Settings())
			{
				require(
					Addictol::IsKnownConfigSection(setting->Section()),
					"validation rejected a registered section");
				require(
					Addictol::IsKnownConfigKey(setting->Section(), setting->Key()),
					"validation rejected a registered key");
			}
			require(
				!Addictol::IsKnownConfigKey("Additional", "bDefinitelyUnknown"),
				"validation accepted an unknown key");
			require(
				!Addictol::IsKnownConfigSection("DefinitelyUnknown"),
				"validation accepted an unknown section");
		});

		runner.test("setting registry type erasure reads and writes REX settings", [] {
			for (const auto* entry : Addictol::SettingRegistry::GetSingleton().Settings())
			{
				const auto value = entry->Value();
				require(MatchesType(entry->Type(), value), "setting value type metadata is wrong");
				require(
					MatchesType(entry->Type(), entry->DefaultValue()),
					"setting default type metadata is wrong");
				require(entry->SetValue(value), "type-erased setting rejected its own value");
				if (const auto& range = entry->NumericRange();
					range && range->minimum && range->maximum)
				{
					require(
						*range->minimum <= *range->maximum,
						"setting numeric range is inverted");
				}
			}

			const auto* setting = Addictol::SettingRegistry::GetSingleton().Find(
				"Additional",
				"bMenuMonochromeIcons");
			require(setting != nullptr, "menu icon setting is not registered");
			const auto original = setting->Value();
			const auto* originalBool = std::get_if<bool>(&original);
			require(originalBool != nullptr, "menu icon setting has the wrong value type");
			require(
				setting->SetValue(!*originalBool),
				"type-erased bool write was rejected");
			require(
				std::get<bool>(setting->Value()) == !*originalBool,
				"type-erased bool write did not reach the REX setting");
			require(
				!setting->SetValue(uint64_t{ 1 }),
				"type-erased setting accepted the wrong value type");
			require(setting->SetValue(original), "menu icon setting could not be restored");
		});
	}
}
