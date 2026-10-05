#include "Harness.h"

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
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
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
		runner.test("startup creates a documented template without active defaults", [] {
			const auto directory = TemporarySettingsDirectory();
			const auto settingsPath = directory / "Addictol.toml";
			bool changed = false;
			std::string error;
			require(
				Addictol::RefreshSettingsDocument(settingsPath, error, &changed),
				"settings template could not be created: " + error);
			require(changed, "missing settings template was not reported as created");
			const auto output = ReadText(settingsPath);
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
			std::filesystem::remove_all(directory);
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

		runner.test("settings writer leaves invalid documents and non-table owned sections intact", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const std::array invalidDocuments{
				"[Additional\nbIgnoreCompatibilityChecks = true\n",
				"Fixes = false\n"
			};
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{ &menu, true }
			};
			for (const auto* invalid : invalidDocuments)
			{
				WriteText(settingsPath, invalid);
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
			}
			std::filesystem::remove_all(directory);
		});

		runner.test("legacy help migrates to inline documentation and is idempotent", [] {
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const std::string existing{
				"# personal note outside managed help\n"
				"[Additional]\n"
				"# >>> Addictol managed help: [Additional]\n"
				"# stale generated text\n"
				"# Generated from the C++ registry. Edit active assignments, not these comments.\n"
				"bIgnoreCompatibilityChecks = false\n"
				"# <<< Addictol managed help\n"
				"# stale redistributable description\n"
				"bUseNewRedistributable = true # stale inline note\n"
				"# keep this nearby note\n"
				"foreign = \"quoted\\\\path\\nvalue\"\n"
				"\n"
				"# keep third party documentation\n"
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
				refreshed.contains("# keep this nearby note") &&
					refreshed.contains("# keep third party documentation"),
				"comments attached to unknown content were lost");
			require(
				!refreshed.contains("stale") &&
					!refreshed.contains("Addictol managed help") &&
					!refreshed.contains("C++ registry") &&
					!refreshed.contains("personal note outside managed help"),
				"legacy help or comments on owned content were retained");
			for (const auto* setting :
				Addictol::SettingRegistry::GetSingleton().Settings())
			{
				if (!root.contains(std::string{ setting->Section() }))
					continue;
				const auto& section = toml::find(root, std::string{ setting->Section() });
				const auto key = std::string{ setting->Key() };
				if (!section.contains(key))
					continue;
				const auto description = "# " + std::string{ setting->Description() };
				require(
					refreshed.contains(
						description + "\n" + key + " = " +
						toml::format(toml::find(section, key))) &&
						CountOccurrences(refreshed, description) == 1,
					"active override is not directly below its current description");
			}
			require(
				toml::find<bool>(root, "Additional", "bUseNewRedistributable"),
				"override following legacy help changed during migration");

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
				"\n# root key note\n"
				"\"root.key\" = [1, 2]\n"
				"# inline table note\n"
				"ThirdParty = { value = 8, nested = { flag = true } }\n"
				"Fixes.foreign = { nested = 3 }\n"
				"Fixes.\"foreign.key\" = [{ nested = [{ id = 5 }] }]\n"
				"Warnings = { nested = { value = 4 } }\n"
				"\n"
				"[ Additional ]\n"
				"foreign = 1\n"
				"\n"
				"[\"Patches\"]\n"
				"foreign = 2\n"
				"\n"
				"# foreign table array note\n"
				"[[Fixes.\"foreign tables\"]]\n"
				"name = \"kept\"\n"
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
				toml::find<std::vector<int>>(root, "root.key") ==
					std::vector<int>{ 1, 2 } &&
					toml::find<int>(root, "ThirdParty", "value") == 8 &&
					toml::find<bool>(root, "ThirdParty", "nested", "flag") &&
					toml::find<std::string>(
						toml::find(root, "Fixes", "foreign tables").as_array().front(),
						"name") == "kept" &&
					toml::find<int>(
						toml::find(
							toml::find(root, "Fixes", "foreign.key").as_array().front(),
							"nested").as_array().front(),
						"id") == 5,
				"quoted keys, root values, or nested arrays of tables changed");
			require(
				refreshed.contains("# root key note") &&
					refreshed.contains("# inline table note") &&
					refreshed.contains("# foreign table array note") &&
					refreshed.find("\"root.key\" =") < refreshed.find("[Additional]") &&
					refreshed.find("[ThirdParty]") > refreshed.find("[Warnings]") &&
					!refreshed.contains("[[Fixes.") &&
					!refreshed.contains("[Fixes.foreign]"),
				"unknown content comments or section placement changed");
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
			std::string repeated;
			require(
				Addictol::BuildSettingsDocumentToml(output, repeated, error) &&
					repeated == output,
				"multiline lookalike refresh was not idempotent");
		});

		runner.test("apply retains only comments attached to unknown content when resetting overrides", [] {
			const auto& menu =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const std::array values{
				Addictol::SettingValueSnapshot{
					&menu,
					menu.DefaultValue()
				}
			};
			struct Fixture
			{
				std::string_view contents;
				std::vector<std::pair<std::string_view, size_t>> notes;
			};
			const std::array fixtures{
				Fixture{
					"# top user note\n\n"
					"# blank-separated note\n\n"
					"[Additional]\n"
					"# keep this reset note\n"
					"bIgnoreCompatibilityChecks = true # inline user note\n"
					"# unknown key note\n"
					"foreign = 17\n\n"
					"# duplicate note\n\n"
					"# duplicate note\n\n"
					"# trailing user note\n"
					"# duplicate note\n",
					{
						{ "# top user note", 0 },
						{ "# blank-separated note", 0 },
						{ "# keep this reset note", 0 },
						{ "# inline user note", 0 },
						{ "# trailing user note", 0 },
						{ "# duplicate note", 0 },
						{ "# unknown key note", 1 }
					},
				},
				Fixture{
					"[Additional]\n"
					"bIgnoreCompatibilityChecks = true\n"
					"foreign = 17 # repeated note\n\n"
					"# repeated note\n",
					{ { "# repeated note", 1 } }
				},
				Fixture{
					"[Additional]\n"
					"bIgnoreCompatibilityChecks = true\n"
					"# repeated note\n"
					"foreign = 17 # repeated note\n\n"
					"# repeated note\n",
					{ { "# repeated note", 2 } }
				}
			};
			for (const auto& [existing, notes] : fixtures)
			{
				std::string output;
				std::string error;
				require(
					Addictol::BuildSettingsOverrideToml(
						existing,
						values,
						output,
						error),
					"comment-rich override could not be rebuilt: " + error);
				for (const auto& [note, count] : notes)
				{
					require(
						CountOccurrences(output, note) == count,
						"Apply changed note multiplicity: " + std::string{ note });
				}
				const auto parsed = toml::try_parse_str(output);
				require(
					parsed.is_ok() &&
						!toml::find(parsed.unwrap(), "Additional").contains(
							"bIgnoreCompatibilityChecks") &&
						toml::find<int64_t>(parsed.unwrap(), "Additional", "foreign") == 17,
					"Apply retained the reset override or changed unknown data");

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
					"comment preservation was not idempotent");
			}
		});

		runner.test("settings path prefers custom and loads only the selected file", [] {
			RegistryValueGuard restore;
			SetRegistryToFactoryDefaults();
			const auto directory = TemporarySettingsDirectory();
			std::filesystem::create_directories(directory);
			const auto settingsPath = directory / "Addictol.toml";
			const auto customPath = directory / "AddictolCustom.toml";
			const auto& canonical =
				Setting("Additional", "bIgnoreCompatibilityChecks");
			const auto& custom = Setting("Patches", "bAchievements");
			const auto canonicalDefault = canonical.DefaultValue();
			const auto customDefault = custom.DefaultValue();
			require(
				Addictol::ResolveSettingsPath(directory) == settingsPath,
				"missing files did not select the base settings path");
			WriteText(
				settingsPath,
				"[Additional]\nbIgnoreCompatibilityChecks = true\n");
			const auto baseContents = ReadText(settingsPath);
			require(
				Addictol::ResolveSettingsPath(directory) == settingsPath,
				"base-only directory did not select Addictol.toml");
			WriteText(
				customPath,
				"[Patches]\nbAchievements = false\n");

			const auto resolved = Addictol::ResolveSettingsPath(directory);
			require(resolved == customPath, "custom settings file was not preferred");
			Addictol::InitializeSettings(resolved);
			require(
				canonical.DefaultValue() == canonicalDefault &&
					custom.DefaultValue() == customDefault,
				"single-file user load mutated a factory default");
			require(
				canonical.Value() == canonicalDefault &&
					ReadText(settingsPath) == baseContents,
				"base settings file was loaded or refreshed despite a custom file");
			require(
				!std::get<bool>(custom.Value()) &&
					ReadText(customPath).contains("# bIgnoreCompatibilityChecks ="),
				"custom settings file was not loaded and documented");

			auto& repository = Addictol::SettingsRepository::GetSingleton();
			auto snapshot = repository.Snapshot();
			SnapshotSetting(snapshot, "Additional", "nQuitGameDelayMs").value = int64_t{ 1375 };
			require(repository.Apply(snapshot).success, "Apply to the selected custom file failed");
			require(
				toml::find<int>(toml::parse_str(ReadText(customPath)), "Additional", "nQuitGameDelayMs") == 1375 &&
					ReadText(settingsPath) == baseContents,
				"repository did not write the active custom file");
			WriteText(
				customPath,
				"[Patches]\nbAchievements = true\n");
			require(
				custom.SetValue(false),
				"path-lifetime fixture could not change the active value");
			REX::FTomlSettingStore::GetSingleton()->Load();
			require(
				std::get<bool>(custom.Value()),
				"settings store path did not survive InitializeSettings");
			std::filesystem::remove(customPath);
			require(
				Addictol::ResolveSettingsPath(directory) == settingsPath,
				"removed custom file did not restore the base settings path");
			require(
				Addictol::ResolveSettingsPath(settingsPath) == settingsPath / "Addictol.toml",
				"filesystem inspection error did not fall back to the base filename");
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

	}
}
