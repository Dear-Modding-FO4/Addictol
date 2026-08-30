#include "Harness.h"

#include <Core/AdConfigValidation.h>
#include <Core/Settings/AdSetting.h>
#include <Core/Settings/AdSettingPersistence.h>
#include <Core/Settings/AdSettingsModel.h>

#include <toml11/single_include/toml.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

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

	[[nodiscard]] const Addictol::SettingEntry& Setting(
		std::string_view a_section,
		std::string_view a_key)
	{
		const auto* setting =
			Addictol::SettingRegistry::GetSingleton().Find(a_section, a_key);
		vmm_tests::require(setting != nullptr, "test setting is not registered");
		return *setting;
	}

	[[nodiscard]] std::filesystem::path TemporarySettingsDirectory()
	{
		const auto unique =
			std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() /
			("addictol-settings-" + std::to_string(unique));
	}

	[[nodiscard]] std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream file{ a_path, std::ios::binary };
		vmm_tests::require(static_cast<bool>(file), "test file could not be opened");
		return {
			std::istreambuf_iterator<char>{ file },
			std::istreambuf_iterator<char>{}
		};
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

		runner.test("setting display categories cover the complete registry", [] {
			std::array<size_t,
				static_cast<size_t>(Addictol::SettingDisplayCategory::kCount)>
				counts{};
			const auto settings =
				Addictol::SettingRegistry::GetSingleton().Settings();
			require(settings.size() == 121, "setting registry size changed");
			for (const auto* setting : settings)
			{
				const auto category =
					static_cast<size_t>(setting->DisplayCategory());
				require(
					category < counts.size(),
					"registered setting has no display category");
				require(
					!setting->DisplayName().empty(),
					"registered setting has no display name");
				++counts[category];
			}
			for (const auto count : counts)
				require(count > 0, "display category has no settings");
		});

		runner.test("settings override output keeps only non-default owned values", [] {
			const auto& menu = Setting("Additional", "bMenu");
			const auto& achievements = Setting("Patches", "bAchievements");
			const auto& bloom = Setting("Patches", "bHighResBloom");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, true },
				Addictol::SettingValueSnapshot{ &achievements, false },
				Addictol::SettingValueSnapshot{ &bloom, bloom.DefaultValue() }
			};
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsOverrideToml(
					{},
					values,
					output,
					error),
				"override TOML could not be built: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "override TOML could not be parsed");
			const auto& root = parsed.unwrap();
			require(
				toml::find<bool>(root, "Additional", "bMenu"),
				"non-default boolean was not written");
			require(
				!toml::find<bool>(root, "Patches", "bAchievements"),
				"setting was written under the wrong section");
			require(
				!toml::find(root, "Patches").contains("bHighResBloom"),
				"default-valued setting was written");
		});

		runner.test("settings override output groups and round trips every value type", [] {
			const auto& boolean = Setting("Additional", "bMenu");
			const auto& floating = Setting("Additional", "fLocalMapScaleFactor");
			const auto& signedInteger = Setting("Additional", "nQuitGameDelayMs");
			const auto& unsignedInteger = Setting("Additional", "uMenuRefreshMs");
			const auto& string = Setting("Additional", "sMenuToggleKey");
			const std::string quoted{ "F\"11\\path\nnext" };
			const std::array values{
				Addictol::SettingValueSnapshot{ &boolean, true },
				Addictol::SettingValueSnapshot{ &floating, 2.25 },
				Addictol::SettingValueSnapshot{ &signedInteger, int64_t{ 1234 } },
				Addictol::SettingValueSnapshot{ &unsignedInteger, uint64_t{ 777 } },
				Addictol::SettingValueSnapshot{ &string, quoted }
			};
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsOverrideToml(
					{},
					values,
					output,
					error),
				"typed override TOML could not be built: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "typed override TOML could not be parsed");
			const auto& root = parsed.unwrap();
			require(
				toml::find<bool>(root, "Additional", "bMenu"),
				"boolean did not round trip");
			require(
				toml::find<double>(
					root,
					"Additional",
					"fLocalMapScaleFactor") == 2.25,
				"float did not round trip");
			require(
				toml::find<int64_t>(
					root,
					"Additional",
					"nQuitGameDelayMs") == 1234,
				"signed integer did not round trip");
			require(
				toml::find<int64_t>(
					root,
					"Additional",
					"uMenuRefreshMs") == 777,
				"unsigned integer did not round trip");
			require(
				toml::find<std::string>(
					root,
					"Additional",
					"sMenuToggleKey") == quoted,
				"quoted string did not round trip");
		});

		runner.test("settings override output preserves unknown existing keys", [] {
			const auto& menu = Setting("Additional", "bMenu");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, menu.DefaultValue() }
			};
			const std::string existing{
				"[Additional]\n"
				"bMenu = true\n"
				"foreign = 17\n"
				"\n"
				"[ThirdParty]\n"
				"name = \"keep\"\n"
			};
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsOverrideToml(
					existing,
					values,
					output,
					error),
				"existing override TOML could not be rebuilt: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "rebuilt override TOML could not be parsed");
			const auto& root = parsed.unwrap();
			require(
				toml::find<int64_t>(root, "Additional", "foreign") == 17,
				"unknown key in an owned section was dropped");
			require(
				toml::find<std::string>(root, "ThirdParty", "name") == "keep",
				"unknown section was dropped");
			require(
				!toml::find(root, "Additional").contains("bMenu"),
				"default owned key was retained");
		});

		runner.test("settings writer never targets the shipped base file", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto base = directory / "Addictol.toml";
			const auto custom = directory / "AddictolCustom.toml";
			{
				std::ofstream file{ base, std::ios::binary };
				file << "shipped-base-sentinel\n";
			}
			const auto& menu = Setting("Additional", "bMenu");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, true }
			};
			std::string error;
			require(
				Addictol::WriteSettingsOverrideFile(custom, values, error),
				"custom override file could not be written: " + error);
			require(
				ReadText(base) == "shipped-base-sentinel\n",
				"shipped base file was modified");
			require(
				std::filesystem::exists(custom),
				"custom override file was not created");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings writer leaves an unreadable custom document intact", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto custom = directory / "AddictolCustom.toml";
			const std::string invalid{ "[Additional\nbMenu = true\n" };
			{
				std::ofstream file{ custom, std::ios::binary };
				file << invalid;
			}
			const auto& menu = Setting("Additional", "bMenu");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, true }
			};
			std::string error;
			require(
				!Addictol::WriteSettingsOverrideFile(custom, values, error),
				"invalid custom document was overwritten");
			require(
				ReadText(custom) == invalid,
				"failed write truncated the existing custom document");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings draft applies each changed field once", [] {
			const auto committed =
				Addictol::SettingsRepository::GetSingleton().Snapshot();
			auto state = Addictol::BeginSettingsDraft(committed);
			require(!Addictol::SettingsDraftDiffers(state), "new draft was dirty");
			state.entries[0].draft =
				!std::get<bool>(state.entries[0].draft);
			state.entries[1].draft =
				!std::get<bool>(state.entries[1].draft);
			require(
				Addictol::SettingsDraftPendingCount(state) == 2,
				"dirty count did not include both changes");
			const auto commit = Addictol::PrepareSettingsDraftApply(state);
			require(
				commit.values.size() == state.entries.size(),
				"apply did not commit the whole draft");
			require(
				commit.changedIndices == std::vector<size_t>{ 0, 1 },
				"apply did not identify each changed field exactly once");
			Addictol::CompleteSettingsDraftApply(state, commit);
			require(
				!Addictol::SettingsDraftDiffers(state),
				"completed apply left the draft dirty");
		});

		runner.test("settings draft revert leave and global reset are non-persistent", [] {
			auto committed =
				Addictol::SettingsRepository::GetSingleton().Snapshot();
			auto state = Addictol::BeginSettingsDraft(committed);
			const auto editable = std::ranges::find_if(
				state.entries,
				[](const Addictol::SettingDraftEntry& a_entry) {
					return Addictol::IsSettingsPageEditable(*a_entry.setting) &&
						a_entry.committed == a_entry.setting->DefaultValue() &&
						std::holds_alternative<bool>(a_entry.committed);
				});
			require(editable != state.entries.end(), "no editable boolean setting found");
			editable->draft = !std::get<bool>(editable->committed);
			Addictol::RevertSettingsDraft(state);
			require(
				editable->draft == editable->committed,
				"revert did not restore the committed value");
			editable->draft = !std::get<bool>(editable->committed);
			Addictol::LeaveSettingsDraft(state);
			require(!state.active, "leaving did not deactivate the draft");
			require(
				editable->draft == editable->committed,
				"leaving did not discard the draft");

			state = Addictol::BeginSettingsDraft(committed);
			const auto resettable = std::ranges::find_if(
				state.entries,
				[](const Addictol::SettingDraftEntry& a_entry) {
					return Addictol::IsSettingsPageEditable(*a_entry.setting) &&
						std::holds_alternative<bool>(a_entry.draft);
				});
			const auto hostOwned = std::ranges::find_if(
				state.entries,
				[](const Addictol::SettingDraftEntry& a_entry) {
					return Addictol::IsHostPresentationSetting(*a_entry.setting) &&
						std::holds_alternative<bool>(a_entry.draft);
				});
			require(
				resettable != state.entries.end() &&
					hostOwned != state.entries.end(),
				"reset authority test settings were not found");
			resettable->draft = !std::get<bool>(
				resettable->setting->DefaultValue());
			hostOwned->draft = !std::get<bool>(
				hostOwned->setting->DefaultValue());
			const auto hostDraft = hostOwned->draft;
			Addictol::ResetSettingsDraftToDefaults(state);
			require(
				resettable->draft == resettable->setting->DefaultValue(),
				"global reset did not populate the editable draft default");
			require(
				resettable->committed == committed[
					static_cast<size_t>(resettable - state.entries.begin())].value,
				"global reset changed committed state");
			require(
				hostOwned->draft == hostDraft,
				"global reset crossed the host presentation authority boundary");
		});

		runner.test("settings filters search metadata and modified values", [] {
			const auto& menu = Setting("Additional", "bMenu");
			Addictol::SettingFilter filter{ "bMenu", false };
			require(
				Addictol::MatchesSettingFilter(
					menu,
					menu.DefaultValue(),
					filter),
				"key search did not match");
			filter.search = "Menu";
			require(
				Addictol::MatchesSettingFilter(
					menu,
					menu.DefaultValue(),
					filter),
				"display-name search did not match");
			filter.search = "diagnostics window";
			require(
				Addictol::MatchesSettingFilter(
					menu,
					menu.DefaultValue(),
					filter),
				"description search did not match");
			filter = { {}, true };
			require(
				!Addictol::MatchesSettingFilter(
					menu,
					menu.DefaultValue(),
					filter),
				"modified-only included a default value");
			require(
				Addictol::MatchesSettingFilter(menu, true, filter),
				"modified-only excluded a changed value");
		});

		runner.test("settings reset predicate and control selection follow metadata", [] {
			const auto& menu = Setting("Additional", "bMenu");
			const auto& toggle = Setting("Additional", "sMenuToggleKey");
			const auto& font = Setting("Additional", "sMenuBodyFontFamily");
			const auto& opacity = Setting("Additional", "fMenuWindowOpacity");
			const auto& maxStdio = Setting("Fixes", "nMaxStdIO");
			const auto& maxPapyrus =
				Setting("Additional", "nMaxPapyrusOpsPerFrame");
			require(
				!Addictol::IsSettingModified(menu, menu.DefaultValue()),
				"default value exposed reset");
			require(
				Addictol::IsSettingModified(menu, true),
				"changed value did not expose reset");
			require(
				Addictol::SelectSettingControl(menu) ==
					Addictol::SettingControlKind::kCheckbox,
				"boolean did not select a checkbox");
			require(
				Addictol::SelectSettingControl(toggle) ==
					Addictol::SettingControlKind::kCombo,
				"known string set did not select a combo");
			require(
				Addictol::SelectSettingControl(font) ==
					Addictol::SettingControlKind::kTextInput,
				"free string did not select text input");
			require(
				Addictol::SelectSettingControl(opacity) ==
					Addictol::SettingControlKind::kSlider,
				"fully bounded number did not select a slider");
			require(
				Addictol::SelectSettingControl(maxStdio) ==
					Addictol::SettingControlKind::kDrag,
				"partially bounded number did not select a drag");
			require(
				Addictol::SelectSettingControl(maxPapyrus) ==
					Addictol::SettingControlKind::kNumericInput,
				"unbounded number did not select numeric input");
		});

		runner.test("settings edit authority keeps host presentation in the gear panel", [] {
			require(
				Addictol::IsSettingsPageEditable(
					Setting("Additional", "bMenu")) &&
					Addictol::IsSettingsPageEditable(
						Setting("Additional", "sMenuToggleKey")) &&
					Addictol::IsSettingsPageEditable(
						Setting("Additional", "uMenuRefreshMs")),
				"Addictol host configuration was not editable on the settings page");
			constexpr std::array hostPresentationKeys{
				std::string_view{ "bMenuMonochromeIcons" },
				std::string_view{ "sMenuAccentColor" },
				std::string_view{ "fMenuWindowOpacity" },
				std::string_view{ "bMenuBackgroundBlur" },
				std::string_view{ "fMenuBackgroundBlurStrength" },
				std::string_view{ "fMenuUiScale" },
				std::string_view{ "sMenuBodyFontFamily" }
			};
			for (const auto key : hostPresentationKeys)
			{
				require(
					!Addictol::IsSettingsPageEditable(
						Setting("Additional", key)),
					"host presentation setting gained duplicate edit authority");
			}
		});
	}
}
