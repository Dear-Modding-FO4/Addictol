#include "Harness.h"

#include <Core/AdConfigValidation.h>
#include <Core/Settings/AdSetting.h>
#include <Core/Settings/AdSettingPersistence.h>
#include <Core/Settings/AdSettings.h>
#include <Core/Settings/AdSettingsModel.h>

#include <toml11/single_include/toml.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	using SettingKey = std::pair<std::string, std::string>;

	class RegistryValueGuard
	{
	public:
		RegistryValueGuard()
		{
			for (const auto* setting :
				Addictol::SettingRegistry::GetSingleton().Settings())
				m_values.push_back({ setting, setting->Value() });
		}

		~RegistryValueGuard()
		{
			for (const auto& item : m_values)
				(void)item.setting->SetValue(item.value);
		}

	private:
		std::vector<Addictol::SettingValueSnapshot> m_values;
	};

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

	// Derived so flipping a shipped default cannot silently invalidate these fixtures.
	[[nodiscard]] bool CompatibilityDefault()
	{
		return std::get<bool>(
			Setting("Additional", "bIgnoreCompatibilityChecks").DefaultValue());
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

	void WriteText(
		const std::filesystem::path& a_path,
		std::string_view a_contents)
	{
		std::ofstream file{
			a_path,
			std::ios::binary | std::ios::trunc
		};
		vmm_tests::require(static_cast<bool>(file), "test file could not be created");
		file << a_contents;
		vmm_tests::require(static_cast<bool>(file), "test file could not be written");
	}

	[[nodiscard]] size_t CountOccurrences(
		std::string_view a_text,
		std::string_view a_needle)
	{
		size_t count = 0;
		size_t position = 0;
		while ((position = a_text.find(a_needle, position)) !=
			std::string_view::npos)
		{
			++count;
			position += a_needle.size();
		}
		return count;
	}

	[[nodiscard]] std::string FormatSettingValue(
		const Addictol::SettingValue& a_value)
	{
		return std::visit(
			[](const auto& a_item) {
				using T = std::remove_cvref_t<decltype(a_item)>;
				if constexpr (std::is_same_v<T, uint64_t>)
					return toml::format(
						toml::value{ static_cast<int64_t>(a_item) });
				else
					return toml::format(toml::value{ a_item });
			},
			a_value);
	}

	void SetRegistryToFactoryDefaults()
	{
		for (const auto* setting :
			Addictol::SettingRegistry::GetSingleton().Settings())
		{
			vmm_tests::require(
				setting->SetValue(setting->DefaultValue()),
				"test could not restore a factory setting");
		}
	}

	[[nodiscard]] Addictol::SettingValueSnapshot& SnapshotSetting(
		std::vector<Addictol::SettingValueSnapshot>& a_snapshot,
		std::string_view a_section,
		std::string_view a_key)
	{
		const auto position = std::ranges::find_if(
			a_snapshot,
			[&](const Addictol::SettingValueSnapshot& a_item) {
				return a_item.setting->Section() == a_section &&
					a_item.setting->Key() == a_key;
			});
		vmm_tests::require(
			position != a_snapshot.end(),
			"settings snapshot does not contain the requested key");
		return *position;
	}
}

namespace vmm_tests
{
	void run_setting_registry_checks(Runner& runner)
	{
		runner.test("setting registry preserves metadata and stable enumeration", [] {
			const auto first = Addictol::SettingRegistry::GetSingleton().Settings();
			const auto second = Addictol::SettingRegistry::GetSingleton().Settings();
			require(first.data() == second.data(), "registry enumeration storage changed");
			require(first.size() == second.size(), "registry enumeration size changed");
			std::set<SettingKey> registered;
			for (size_t index = 0; index < first.size(); ++index)
			{
				const auto* setting = first[index];
				require(setting != nullptr, "registry contains a null setting");
				require(setting == second[index], "registry enumeration order changed");
				require(
					registered.emplace(setting->Section(), setting->Key()).second,
					"registry contains a duplicate section and key");
				require(!setting->Description().empty(), "registered setting has no description");
				const auto timing = setting->ApplyTiming();
				require(
					timing == Addictol::SettingApplyTiming::kImmediate ||
						timing == Addictol::SettingApplyTiming::kNextLaunch,
					"registered setting has no explicit apply timing");
				require(
					static_cast<size_t>(setting->DisplayCategory()) <
						static_cast<size_t>(Addictol::SettingDisplayCategory::kCount),
					"registered setting has no display category");
				require(!setting->DisplayName().empty(), "registered setting has no display name");
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
			require(
				registered.size() == first.size(),
				"registered settings were not unique");
		});

		runner.test("factory defaults match the approved release values", [] {
			require(
				!std::get<bool>(
					Setting("Additional", "bUseNewRedistributable").DefaultValue()),
				"bUseNewRedistributable factory default is not false");
			require(
				std::get<int64_t>(
					Setting("Additional", "nQuitGameDelayMs").DefaultValue()) == 1000,
				"nQuitGameDelayMs factory default is not 1000");
			require(
				std::get<bool>(
					Setting("Patches", "bArchiveLimits").DefaultValue()),
				"bArchiveLimits factory default is not true");
			require(
				!std::get<bool>(
					Setting("Patches", "bInputSwitch").DefaultValue()),
				"bInputSwitch factory default is not false");
			require(
				!std::get<bool>(
					Setting("Fixes", "bAltTabFullscreen").DefaultValue()),
				"bAltTabFullscreen factory default is not false");
		});

		runner.test("generated settings document covers the registry without active defaults", [] {
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsDocumentToml({}, output, error),
				"settings template could not be generated: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "generated settings template is not valid TOML");
			const auto& root = parsed.unwrap();
			for (const auto* setting :
				Addictol::SettingRegistry::GetSingleton().Settings())
			{
				const auto& section = toml::find(
					root,
					std::string{ setting->Section() });
				require(
					!section.contains(std::string{ setting->Key() }),
					"generated template contains an active factory assignment");
				const auto expectedDescription =
					"# " + std::string{ setting->Description() };
				require(
					output.contains(expectedDescription),
					"generated template does not contain a registry description");
				const auto expectedAssignment =
					"# " + std::string{ setting->Key() } + " = " +
					FormatSettingValue(setting->DefaultValue());
				require(
					CountOccurrences(output, expectedAssignment) == 1,
					"generated template does not contain exactly one commented default");
			}
		});

		runner.test("setting registry resolves module gate pointers", [] {
			const auto* setting =
				Addictol::SettingRegistry::GetSingleton().Find(
					&Addictol::bAdditionalIgnoreCompatibilityChecks);
			require(setting != nullptr, "module gate pointer was not registered");
			require(
				setting->Section() == "Additional" &&
					setting->Key() == "bIgnoreCompatibilityChecks",
				"module gate pointer resolved to the wrong key");
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
				"bIgnoreCompatibilityChecks");
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

		runner.test("settings override output keeps only non-default owned values", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const auto& achievements = Setting("Patches", "bAchievements");
			const auto& bloom = Setting("Patches", "bHighResBloom");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, !CompatibilityDefault() },
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
				toml::find(root, "Additional").contains("bIgnoreCompatibilityChecks") &&
					toml::find<bool>(
						root,
						"Additional",
						"bIgnoreCompatibilityChecks") == !CompatibilityDefault(),
				"non-default boolean was not written");
			require(
				!toml::find<bool>(root, "Patches", "bAchievements"),
				"setting was written under the wrong section");
			require(
				!toml::find(root, "Patches").contains("bHighResBloom"),
				"default-valued setting was written");
		});

		runner.test("settings override output groups and round trips every value type", [] {
			const auto& boolean =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const auto& floating = Setting("Additional", "fLocalMapScaleFactor");
			const auto& signedInteger = Setting("Additional", "nQuitGameDelayMs");
			const auto& unsignedInteger = Setting("Additional", "uMenuRefreshMs");
			const auto& string = Setting("Additional", "sAllocator");
			const std::string quoted{ "F\"11\\path\nnext" };
			const std::array values{
				Addictol::SettingValueSnapshot{ &boolean, !CompatibilityDefault() },
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
				toml::find<bool>(
					root,
					"Additional",
					"bIgnoreCompatibilityChecks") == !CompatibilityDefault(),
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
					"sAllocator") == quoted,
				"quoted string did not round trip");
		});

		runner.test("settings override output preserves unknown existing keys", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, menu.DefaultValue() }
			};
			const std::string existing{
				"[Additional]\n"
				"bIgnoreCompatibilityChecks = true\n"
				"foreign = 17\n"
				"\n"
				"[ThirdParty]\n"
				"name = \"keep\"\n"
				"\n"
				"[EmptyThirdParty]\n"
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
				root.contains("EmptyThirdParty") &&
					toml::find(root, "EmptyThirdParty").as_table().empty(),
				"empty unknown section was dropped");
			require(
				!toml::find(root, "Additional").contains(
					"bIgnoreCompatibilityChecks"),
				"default owned key was retained");
		});

		runner.test("settings writer creates the canonical document with the requested override", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, !CompatibilityDefault() }
			};
			std::string error;
			require(
				Addictol::WriteSettingsOverrideFile(settingsPath, values, error),
				"settings override file could not be written: " + error);
			require(
				std::filesystem::exists(settingsPath),
				"settings override file was not created");
			const auto root = toml::parse_str(ReadText(settingsPath));
			require(
				toml::find<bool>(root, "Additional", "bIgnoreCompatibilityChecks") ==
					!CompatibilityDefault(),
				"new settings document lost its requested override");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings writer updates a valid existing canonical document", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			WriteText(
				settingsPath,
				"[Additional]\nbIgnoreCompatibilityChecks = true\nzUnowned = 7\n");
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, !CompatibilityDefault() }
			};
			std::string error;
			require(
				Addictol::WriteSettingsOverrideFile(settingsPath, values, error),
				"existing settings document could not be rewritten: " + error);
			const auto root = toml::parse_str(ReadText(settingsPath));
			require(
				toml::find<bool>(
					root,
					"Additional",
					"bIgnoreCompatibilityChecks") == !CompatibilityDefault(),
				"existing settings document lost its override");
			require(
				toml::find<int64_t>(root, "Additional", "zUnowned") == 7,
				"existing settings document lost an unowned key");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings writer leaves an unparseable canonical document intact", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const std::string invalid{
				"[Additional\nbIgnoreCompatibilityChecks = true\n"
			};
			WriteText(settingsPath, invalid);
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, true }
			};
			std::string error;
			bool changed = true;
			require(
				!Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"invalid settings document was refreshed");
			require(
				!error.empty() && ReadText(settingsPath) == invalid,
				"documentation failure was not surfaced without modifying the file");
			require(
				!Addictol::WriteSettingsOverrideFile(settingsPath, values, error),
				"invalid settings document was overwritten");
			require(
				ReadText(settingsPath) == invalid,
				"failed write truncated the existing settings document");
			std::filesystem::remove_all(directory);
		});

		runner.test("startup documentation refresh preserves user-owned content and is idempotent", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const std::string existing{
				"# personal note outside managed help\n"
				"[Additional]\n"
				"# >>> Addictol managed help: [Additional]\n"
				"# stale generated text\n"
				"bIgnoreCompatibilityChecks = false\n"
				"# <<< Addictol managed help\n"
				"# keep this nearby note\n"
				"foreign = \"quoted\\\\path\\nvalue\"\n"
				"\n"
				"[ThirdParty]\n"
				"enabled = true\n"
			};
			WriteText(settingsPath, existing);

			bool changed = false;
			std::string error;
			require(
				Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"settings documentation could not be refreshed: " + error);
			require(changed, "first documentation refresh reported no change");
			const auto refreshed = ReadText(settingsPath);
			const auto root = toml::parse_str(refreshed);
			require(
				!toml::find<bool>(
					root,
					"Additional",
					"bIgnoreCompatibilityChecks"),
				"active assignment inside managed help was discarded");
			require(
				toml::find<std::string>(
					root,
					"Additional",
					"foreign") == "quoted\\path\nvalue",
				"escaped unknown string changed during documentation refresh");
			require(
				toml::find<bool>(root, "ThirdParty", "enabled"),
				"unknown section changed during documentation refresh");
			require(
				refreshed.contains("# personal note outside managed help") &&
					refreshed.contains("# keep this nearby note"),
				"user comments outside managed help were lost");
			require(
				!refreshed.contains("stale generated text"),
				"stale managed help was retained");

			changed = true;
			require(
				Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"second documentation refresh failed: " + error);
			require(!changed, "identical documentation was rewritten");
			require(
				ReadText(settingsPath) == refreshed,
				"repeated documentation refresh changed output");
			std::filesystem::remove_all(directory);
		});

		runner.test("startup documentation uses parsed table structure for valid TOML forms", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const std::string existing{
				"\xEF\xBB\xBF"
				"Fixes.foreign = { nested = 3 }\n"
				"Warnings = { nested = { value = 4 } }\n"
				"\n"
				"[ Additional ]\n"
				"foreign = 1\n"
				"\n"
				"[\"Patches\"]\n"
				"foreign = 2\n"
			};
			WriteText(settingsPath, existing);

			bool changed = false;
			std::string error;
			require(
				Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"valid alternate TOML forms could not be refreshed: " + error);
			require(changed, "alternate TOML forms reported no documentation change");
			const auto refreshed = ReadText(settingsPath);
			require(
				refreshed.starts_with("\xEF\xBB\xBF"),
				"UTF-8 BOM was not preserved");
			const auto parsed = toml::try_parse_str(refreshed);
			require(parsed.is_ok(), "refreshed alternate TOML forms are invalid");
			const auto& root = parsed.unwrap();
			require(
				toml::find<int64_t>(root, "Additional", "foreign") == 1 &&
					toml::find<int64_t>(root, "Patches", "foreign") == 2 &&
					toml::find<int64_t>(
						root,
						"Fixes",
						"foreign",
						"nested") == 3 &&
					toml::find<int64_t>(
						root,
						"Warnings",
						"nested",
						"value") == 4,
				"documentation refresh changed dotted, inline, or quoted-table data");
			require(
				refreshed.starts_with("\xEF\xBB\xBF[Fixes]\n") &&
					refreshed.find("\n[Warnings]\n") != std::string::npos,
				"documentation refresh did not canonicalize dotted or inline owned tables");
			auto uncommented = refreshed;
			const std::string example{
				"# bAltTabFullscreen = false"
			};
			const auto examplePosition = uncommented.find(example);
			require(
				examplePosition != std::string::npos,
				"canonicalized section did not contain its unqualified example");
			uncommented.erase(examplePosition, 2);
			const auto uncommentedRoot = toml::try_parse_str(uncommented);
			require(
				uncommentedRoot.is_ok() &&
					!toml::find<bool>(
						uncommentedRoot.unwrap(),
						"Fixes",
						"bAltTabFullscreen"),
				"uncommented canonicalized example was not an effective override");

			changed = true;
			require(
				Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"second alternate-form refresh failed: " + error);
			require(!changed, "alternate-form documentation was not idempotent");
			require(
				ReadText(settingsPath) == refreshed,
				"repeated alternate-form refresh changed output");

			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					!CompatibilityDefault()
				}
			};
			std::string applied;
			require(
				Addictol::BuildSettingsOverrideToml(
					refreshed,
					values,
					applied,
					error),
				"alternate TOML forms could not be applied: " + error);
			require(
				applied.starts_with("\xEF\xBB\xBF"),
				"Apply discarded the UTF-8 BOM");
			const auto appliedRoot = toml::try_parse_str(applied);
			require(appliedRoot.is_ok(), "applied alternate TOML forms are invalid");
			require(
				toml::find<bool>(
					appliedRoot.unwrap(),
					"Additional",
					"bIgnoreCompatibilityChecks") ==
						!CompatibilityDefault() &&
					toml::find<int64_t>(
						appliedRoot.unwrap(),
						"Fixes",
						"foreign",
						"nested") == 3 &&
					toml::find<int64_t>(
						appliedRoot.unwrap(),
						"Warnings",
						"nested",
						"value") == 4,
				"Apply changed alternate-form unknown data or lost the owned override");
			std::filesystem::remove_all(directory);
		});

		runner.test("managed help markers and section lookalikes inside multiline strings are data", [] {
			const std::string existing{
				"[ThirdParty]\n"
				"basic = \"\"\"\n"
				"[Additional]\n"
				"# >>> Addictol managed help: [Additional]\n"
				"# <<< Addictol managed help\n"
				"\"\"\"\n"
				"literal = '''\n"
				"[Patches]\n"
				"# >>> Addictol managed help: [Patches]\n"
				"# <<< Addictol managed help\n"
				"'''\n"
				"nested = { value = 9 }\n"
				"\n"
				"[Additional]\n"
				"foreign = 17\n"
			};
			const auto original = toml::parse_str(existing);
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsDocumentToml(
					existing,
					output,
					error),
				"multiline lookalike document could not be refreshed: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "multiline lookalike output is invalid TOML");
			const auto& root = parsed.unwrap();
			require(
				toml::find<std::string>(root, "ThirdParty", "basic") ==
						toml::find<std::string>(
							original,
							"ThirdParty",
							"basic") &&
					toml::find<std::string>(root, "ThirdParty", "literal") ==
						toml::find<std::string>(
							original,
							"ThirdParty",
							"literal") &&
					toml::find<int64_t>(
						root,
						"ThirdParty",
						"nested",
						"value") == 9,
				"multiline string or nested unknown data changed during refresh");
			require(
				toml::find<int64_t>(root, "Additional", "foreign") == 17,
				"real section data changed while handling multiline lookalikes");
		});

		runner.test("startup documentation creates a complete template when missing", [] {
			const auto directory = TemporarySettingsDirectory();
			const auto settingsPath = directory / "Addictol.toml";
			bool changed = false;
			std::string error;
			require(
				Addictol::RefreshSettingsDocument(
					settingsPath,
					error,
					&changed),
				"missing settings template could not be created: " + error);
			require(changed, "missing settings template was not reported as created");
			require(
				std::filesystem::exists(settingsPath),
				"missing settings template was not created");
			const auto root = toml::parse_str(ReadText(settingsPath));
			for (const auto* setting :
				Addictol::SettingRegistry::GetSingleton().Settings())
			{
				require(
					!toml::find(
						root,
						std::string{ setting->Section() }).contains(
						std::string{ setting->Key() }),
					"created template contains an active factory assignment");
			}
			std::filesystem::remove_all(directory);
		});

		runner.test("apply preserves notes while removing default-valued owned keys", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					menu.DefaultValue()
				}
			};
			const std::string existing{
				"[Additional]\n"
				"# keep this reset note\n"
				"bIgnoreCompatibilityChecks = true # keep inline reset note\n"
				"foreign = 17\n"
			};
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsOverrideToml(
					existing,
					values,
					output,
					error),
				"default-valued key could not be removed: " + error);
			const auto root = toml::parse_str(output);
			require(
				!toml::find(root, "Additional").contains(
					"bIgnoreCompatibilityChecks"),
				"default-valued owned key remained active");
			require(
				toml::find<int64_t>(root, "Additional", "foreign") == 17,
				"unknown data was lost while removing an owned key");
			require(
				output.contains("keep this reset note") &&
					output.contains("keep inline reset note"),
				"comment attached to a removed owned key was lost");
		});

		runner.test("apply preserves free-standing trailing and duplicate user notes", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					menu.DefaultValue()
				}
			};
			const std::string existing{
				"# top user note\n"
				"\n"
				"# blank-separated note\n"
				"\n"
				"[Additional]\n"
				"bIgnoreCompatibilityChecks = true # inline user note\n"
				"foreign = 17\n"
				"\n"
				"# duplicate note\n"
				"\n"
				"# duplicate note\n"
				"\n"
				"[ThirdParty.nested]\n"
				"value = 9\n"
				"\n"
				"# trailing user note\n"
				"# duplicate note\n"
			};
			std::string output;
			std::string error;
			require(
				Addictol::BuildSettingsOverrideToml(
					existing,
					values,
					output,
					error),
				"comment-rich override could not be rebuilt: " + error);
			const auto parsed = toml::try_parse_str(output);
			require(parsed.is_ok(), "comment-rich override output is invalid");
			const auto& root = parsed.unwrap();
			require(
				!toml::find(root, "Additional").contains(
					"bIgnoreCompatibilityChecks") &&
					toml::find<int64_t>(root, "Additional", "foreign") == 17 &&
					toml::find<int64_t>(
						root,
						"ThirdParty",
						"nested",
						"value") == 9,
				"Apply changed owned defaults or unknown nested data");
			require(
				output.contains("# top user note") &&
					output.contains("# blank-separated note") &&
					output.contains("# inline user note") &&
					output.contains("# trailing user note") &&
					CountOccurrences(output, "# duplicate note") == 3,
				"Apply lost free-standing, trailing, inline, or duplicate notes");

			std::string repeated;
			require(
				Addictol::BuildSettingsOverrideToml(
					output,
					values,
					repeated,
					error),
				"repeated comment-rich Apply failed: " + error);
			require(
				repeated == output,
				"repeated comment-rich Apply was not idempotent");
		});

		runner.test("apply preserves same-text attached inline and trailing notes", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					menu.DefaultValue()
				}
			};
			const std::array fixtures{
				std::pair{
					std::string{
						"[Foreign]\n"
						"value = 1 # repeated note\n"
						"\n"
						"# repeated note\n"
					},
					size_t{ 2 }
				},
				std::pair{
					std::string{
						"[Foreign]\n"
						"# repeated note\n"
						"value = 1 # repeated note\n"
						"\n"
						"# repeated note\n"
					},
					size_t{ 3 }
				}
			};
			for (const auto& [existing, expectedCount] : fixtures)
			{
				std::string output;
				std::string error;
				require(
					Addictol::BuildSettingsOverrideToml(
						existing,
						values,
						output,
						error),
					"same-text note fixture could not be rebuilt: " +
						error);
				require(
					CountOccurrences(output, "# repeated note") ==
						expectedCount,
					"Apply lost a same-text attached, inline, or trailing note");
				const auto parsed = toml::try_parse_str(output);
				require(
					parsed.is_ok() &&
						toml::find<int64_t>(
							parsed.unwrap(),
							"Foreign",
							"value") == 1,
					"same-text note preservation changed unknown data");

				std::string repeated;
				require(
					Addictol::BuildSettingsOverrideToml(
						output,
						values,
						repeated,
						error),
					"repeated same-text note Apply failed: " + error);
				require(
					repeated == output,
					"same-text note preservation was not idempotent");
			}
		});

		runner.test("single-file load keeps factory defaults and ignores legacy custom files", [] {
			RegistryValueGuard restore;
			SetRegistryToFactoryDefaults();
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const auto legacyPath = directory / "AddictolCustom.toml";
			const auto& canonical =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const auto& legacy = Setting("Patches", "bAchievements");
			const auto canonicalDefault = canonical.DefaultValue();
			const auto legacyDefault = legacy.DefaultValue();
			WriteText(
				settingsPath,
				"[Additional]\nbIgnoreCompatibilityChecks = true\n");
			WriteText(
				legacyPath,
				"[Patches]\nbAchievements = false\n");

			Addictol::InitializeSettings(settingsPath);
			require(
				canonical.DefaultValue() == canonicalDefault,
				"single-file user load mutated a factory default");
			require(
				std::get<bool>(canonical.Value()),
				"canonical Addictol.toml override was not loaded");
			require(
				legacy.Value() == legacyDefault,
				"legacy AddictolCustom.toml was loaded");
			WriteText(
				settingsPath,
				"[Additional]\nbIgnoreCompatibilityChecks = false\n");
			require(
				canonical.SetValue(true),
				"path-lifetime fixture could not change the active value");
			REX::FTomlSettingStore::GetSingleton()->Load();
			require(
				!std::get<bool>(canonical.Value()),
				"settings store path did not survive InitializeSettings");
			std::filesystem::remove_all(directory);
		});

		runner.test("repository apply reset and reload preserve timing semantics", [] {
			RegistryValueGuard restore;
			SetRegistryToFactoryDefaults();
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			WriteText(
				settingsPath,
				"# retain this user note\n"
				"[ThirdParty]\n"
				"value = 9\n");

			Addictol::SettingsRepository repository{ settingsPath };
			auto values = repository.Snapshot();
			auto& immediate = SnapshotSetting(
				values,
				"Additional",
				"nQuitGameDelayMs");
			auto& nextLaunch = SnapshotSetting(
				values,
				"Patches",
				"bArchiveLimits");
			immediate.value = int64_t{ 1375 };
			nextLaunch.value = false;

			const auto firstApply = repository.Apply(values);
			require(firstApply.success, "settings Apply failed: " + firstApply.error);
			require(firstApply.changed == 2, "settings Apply reported the wrong change count");
			require(
				std::get<int64_t>(immediate.setting->Value()) == 1375,
				"immediate setting did not update during Apply");
			require(
				std::get<bool>(nextLaunch.setting->Value()),
				"next-launch setting changed before reload");
			auto root = toml::parse_str(ReadText(settingsPath));
			require(
				toml::find<int64_t>(
					root,
					"Additional",
					"nQuitGameDelayMs") == 1375 &&
					!toml::find<bool>(
						root,
						"Patches",
						"bArchiveLimits"),
				"Apply did not persist both non-default overrides");

			Addictol::InitializeSettings(settingsPath);
			require(
				!std::get<bool>(nextLaunch.setting->Value()),
				"next-launch setting was not loaded on restart-equivalent initialization");
			require(
				nextLaunch.setting->DefaultValue() == Addictol::SettingValue{ true },
				"restart-equivalent load mutated the next-launch factory default");

			auto draft = Addictol::BeginSettingsDraft(repository.Snapshot());
			Addictol::ResetSettingsDraftToDefaults(draft);
			const auto resetCommit = Addictol::PrepareSettingsDraftApply(draft);
			const auto resetApply = repository.Apply(resetCommit.values);
			require(resetApply.success, "reset Apply failed: " + resetApply.error);
			require(
				std::get<int64_t>(immediate.setting->Value()) == 1000,
				"reset did not update the immediate setting");
			require(
				!std::get<bool>(nextLaunch.setting->Value()),
				"reset updated a next-launch setting before reload");
			const auto resetText = ReadText(settingsPath);
			root = toml::parse_str(resetText);
			require(
				!toml::find(root, "Additional").contains("nQuitGameDelayMs") &&
					!toml::find(root, "Patches").contains("bArchiveLimits"),
				"reset left factory-valued assignments active");
			require(
				resetText.contains("retain this user note") &&
					toml::find<int64_t>(root, "ThirdParty", "value") == 9,
				"reset lost user notes or unknown data");

			Addictol::InitializeSettings(settingsPath);
			require(
				std::get<bool>(nextLaunch.setting->Value()),
				"next-launch reset did not take effect after reload");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings writes report unchanged output and atomic failures", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					menu.DefaultValue()
				}
			};
			bool changed = false;
			std::string error;
			require(
				Addictol::WriteSettingsOverrideFile(
					settingsPath,
					values,
					error,
					&changed),
				"initial settings write failed: " + error);
			require(changed, "initial settings write reported no change");
			const auto first = ReadText(settingsPath);
			changed = true;
			require(
				Addictol::WriteSettingsOverrideFile(
					settingsPath,
					values,
					error,
					&changed),
				"repeated settings write failed: " + error);
			require(!changed, "identical settings output was rewritten");
			require(ReadText(settingsPath) == first, "identical settings output changed");

			const auto blocker = directory / "not-a-directory";
			WriteText(blocker, "block");
			const auto impossible = blocker / "Addictol.toml";
			require(
				!Addictol::WriteSettingsOverrideFile(
					impossible,
					values,
					error),
				"writer unexpectedly succeeded through a file parent");
			require(ReadText(blocker) == "block", "atomic failure modified the blocker file");
			std::filesystem::remove_all(directory);
		});

		runner.test("settings draft applies each changed field once", [] {
			const auto committed =
				Addictol::SettingsRepository::GetSingleton().Snapshot();
			auto state = Addictol::BeginSettingsDraft(committed);
			require(!Addictol::SettingsDraftDiffers(state), "new draft was dirty");
			std::vector<size_t> changed;
			for (size_t index = 0; index < state.entries.size() && changed.size() < 2; ++index)
			{
				auto& entry = state.entries[index];
				if (entry.setting->Type() == Addictol::SettingValueType::kBoolean)
				{
					entry.draft = !std::get<bool>(entry.draft);
					changed.push_back(index);
				}
			}
			require(changed.size() == 2, "draft fixture needs two boolean settings");
			require(
				Addictol::SettingsDraftPendingCount(state) == 2,
				"dirty count did not include both changes");
			const auto commit = Addictol::PrepareSettingsDraftApply(state);
			require(
				commit.values.size() == state.entries.size(),
				"apply did not commit the whole draft");
			require(
				commit.changedIndices == changed,
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
					return a_entry.committed == a_entry.setting->DefaultValue() &&
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
					return std::holds_alternative<bool>(a_entry.draft);
				});
			require(resettable != state.entries.end(), "reset test setting was not found");
			resettable->draft = !std::get<bool>(
				resettable->setting->DefaultValue());
			Addictol::ResetSettingsDraftToDefaults(state);
			require(
				resettable->draft == resettable->setting->DefaultValue(),
				"global reset did not populate the editable draft default");
			require(
				resettable->committed == committed[
					static_cast<size_t>(resettable - state.entries.begin())].value,
				"global reset changed committed state");
		});

		runner.test("settings draft identities resolve across rebuilt entry storage", [] {
			const auto committed =
				Addictol::SettingsRepository::GetSingleton().Snapshot();
			auto first = Addictol::BeginSettingsDraft(committed);
			const auto identity = Addictol::MakeSettingIdentity(
				Setting("Additional", "bIgnoreCompatibilityChecks"));
			const auto* firstEntry =
				Addictol::ResolveSettingDraftEntry(first, identity);
			require(firstEntry != nullptr, "identity did not resolve in the first draft");

			auto rebuilt = Addictol::BeginSettingsDraft(committed);
			auto* rebuiltEntry =
				Addictol::ResolveSettingDraftEntry(rebuilt, identity);
			require(
				rebuiltEntry != nullptr &&
					rebuiltEntry->setting == firstEntry->setting,
				"identity did not resolve after draft storage was rebuilt");
			rebuiltEntry->draft = !std::get<bool>(rebuiltEntry->draft);
			const auto* constEntry = Addictol::ResolveSettingDraftEntry(
				std::as_const(rebuilt),
				identity);
			require(
				constEntry && constEntry->draft == rebuiltEntry->draft,
				"const identity resolution returned a different draft entry");

			Addictol::LeaveSettingsDraft(first);
			require(
				Addictol::ResolveSettingDraftEntry(first, identity) == nullptr,
				"inactive draft storage remained resolvable");
		});

		runner.test("settings draft values recover and clamp before binding", [] {
			const auto& scale = Setting("Additional", "fLocalMapScaleFactor");
			const auto recovered = Addictol::NormalizeSettingDraftValue(
				scale,
				Addictol::SettingValue{
					std::numeric_limits<double>::quiet_NaN() });
			require(
				std::get<double>(recovered) ==
					std::get<double>(scale.DefaultValue()),
				"non-finite float did not recover its default");

			const auto& operations =
				Setting("Additional", "nMaxPapyrusOpsPerFrame");
			const auto signedValue = Addictol::NormalizeSettingDraftValue(
				operations,
				Addictol::SettingValue{
					(std::numeric_limits<int64_t>::max)() });
			require(
				std::get<int64_t>(signedValue) ==
					(std::numeric_limits<int32_t>::max)(),
				"signed input escaped its backing type");

			const auto& refresh = Setting("Additional", "uMenuRefreshMs");
			const auto unsignedValue = Addictol::NormalizeSettingDraftValue(
				refresh,
				Addictol::SettingValue{
					(std::numeric_limits<uint64_t>::max)() });
			require(
				std::get<uint64_t>(unsignedValue) == 2000,
				"unsigned input escaped its declared range");

			const auto& maxStdio = Setting("Fixes", "nMaxStdIO");
			const auto draggedValue = Addictol::NormalizeSettingDraftValue(
				maxStdio,
				Addictol::SettingValue{ int64_t{ 9000 } });
			require(
				std::get<int64_t>(draggedValue) == 8192,
				"partially bounded drag escaped its declared maximum");
		});

		runner.test("settings reset predicate and control selection follow metadata", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const auto& allocator = Setting("Additional", "sAllocator");
			const auto& refresh = Setting("Additional", "uMenuRefreshMs");
			const auto& maxStdio = Setting("Fixes", "nMaxStdIO");
			const auto& maxPapyrus =
				Setting("Additional", "nMaxPapyrusOpsPerFrame");
			require(
				!Addictol::IsSettingModified(menu, menu.DefaultValue()),
				"default value exposed reset");
			require(
				Addictol::IsSettingModified(menu, !CompatibilityDefault()),
				"changed value did not expose reset");
			require(
				Addictol::SelectSettingControl(menu) ==
					Addictol::SettingControlKind::kCheckbox,
				"boolean did not select a checkbox");
			require(
				Addictol::SelectSettingControl(allocator) ==
					Addictol::SettingControlKind::kCombo,
				"known string set did not select a combo");
			require(
				Addictol::SelectSettingControl(refresh) ==
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

		runner.test("host-owned menu settings leave the Addictol registry", [] {
			constexpr std::array hostOwnedKeys{
				std::string_view{ "bMenu" },
				std::string_view{ "sMenuToggleKey" },
				std::string_view{ "bMenuMonochromeIcons" },
				std::string_view{ "sMenuAccentColor" },
				std::string_view{ "fMenuWindowOpacity" },
				std::string_view{ "bMenuBackgroundBlur" },
				std::string_view{ "fMenuBackgroundBlurStrength" },
				std::string_view{ "fMenuUiScale" },
				std::string_view{ "sMenuBodyFontFamily" }
			};
			for (const auto key : hostOwnedKeys)
			{
				require(
					Addictol::SettingRegistry::GetSingleton().Find(
						"Additional",
						key) == nullptr,
					"host-owned setting remained in Addictol");
			}
			require(
				Addictol::SettingRegistry::GetSingleton().Find(
					"Additional",
					"uMenuRefreshMs") != nullptr,
				"client refresh setting left Addictol");
		});
	}
}
