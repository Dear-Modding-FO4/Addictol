#include "../Addictol/Include/Menu/AdMenuTargets.h"
#include "../Addictol/Include/Menu/AdMenuModules.h"
#include "../Addictol/Include/Menu/AdMenuSettings.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include "../Addictol/Include/Modules/AdFacegenExceptions.h"
#include "../Addictol/Include/Modules/AdModuleInputSwitch.h"
#include "Harness.h"

#include <Core/Settings/AdSettingsModel.h>
#include <INI/SimpleIni.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuHome.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol;
		using namespace Addictol::Menu;

		struct ExpectedLogLevel
		{
			LogControl::Level level;
			std::string_view name;
		};

		struct OutcomeStatus
		{
			ModuleOutcome outcome;
		};

		template <class... Arguments>
		DMUI_Result DMUI_CALL UnsupportedUIOperation(Arguments...) noexcept
		{
			return DMUI_RESULT_UNSUPPORTED_ABI;
		}

		const DMUI_UIAPI kMockUI{
			.structSize = sizeof(DMUI_UIAPI),
			.abiVersion = DMUI_UI_ABI_CURRENT,
			.revision = DMUI_UI_REVISION_1,
			.getStyleMetrics = &UnsupportedUIOperation,
			.beginCombo = &UnsupportedUIOperation,
			.endCombo = &UnsupportedUIOperation,
			.beginDisabled = &UnsupportedUIOperation,
			.endDisabled = &UnsupportedUIOperation,
			.beginTable = &UnsupportedUIOperation,
			.endTable = &UnsupportedUIOperation,
			.beginTooltip = &UnsupportedUIOperation,
			.endTooltip = &UnsupportedUIOperation,
			.button = &UnsupportedUIOperation,
			.calcTextSize = &UnsupportedUIOperation,
			.checkbox = &UnsupportedUIOperation,
			.collapsingHeader = &UnsupportedUIOperation,
			.collapsingHeaderVisible = &UnsupportedUIOperation,
			.dragScalar = &UnsupportedUIOperation,
			.dummy = &UnsupportedUIOperation,
			.getContentRegionAvail = &UnsupportedUIOperation,
			.getCursorScreenPos = &UnsupportedUIOperation,
			.getFontSize = &UnsupportedUIOperation,
			.getFrameHeight = &UnsupportedUIOperation,
			.getStyleColor = &UnsupportedUIOperation,
			.getTextLineHeightWithSpacing = &UnsupportedUIOperation,
			.indent = &UnsupportedUIOperation,
			.inputScalar = &UnsupportedUIOperation,
			.inputText = &UnsupportedUIOperation,
			.inputTextMultiline = &UnsupportedUIOperation,
			.inputTextWithHint = &UnsupportedUIOperation,
			.isItemDeactivatedAfterEdit = &UnsupportedUIOperation,
			.isItemHovered = &UnsupportedUIOperation,
			.popID = &UnsupportedUIOperation,
			.popStyleColor = &UnsupportedUIOperation,
			.popTextWrapPos = &UnsupportedUIOperation,
			.progressBar = &UnsupportedUIOperation,
			.pushIDString = &UnsupportedUIOperation,
			.pushIDRange = &UnsupportedUIOperation,
			.pushIDValue = &UnsupportedUIOperation,
			.pushStyleColorU32 = &UnsupportedUIOperation,
			.pushStyleColor = &UnsupportedUIOperation,
			.pushTextWrapPos = &UnsupportedUIOperation,
			.sameLine = &UnsupportedUIOperation,
			.selectable = &UnsupportedUIOperation,
			.selectableToggle = &UnsupportedUIOperation,
			.separator = &UnsupportedUIOperation,
			.setClipboardText = &UnsupportedUIOperation,
			.setCursorScreenPos = &UnsupportedUIOperation,
			.setItemDefaultFocus = &UnsupportedUIOperation,
			.setNextItemWidth = &UnsupportedUIOperation,
			.setTooltipText = &UnsupportedUIOperation,
			.sliderScalar = &UnsupportedUIOperation,
			.spacing = &UnsupportedUIOperation,
			.tableHeadersRow = &UnsupportedUIOperation,
			.tableNextColumn = &UnsupportedUIOperation,
			.tableNextRow = &UnsupportedUIOperation,
			.tableSetColumnIndex = &UnsupportedUIOperation,
			.tableSetupColumn = &UnsupportedUIOperation,
			.tableSetupScrollFreeze = &UnsupportedUIOperation,
			.text = &UnsupportedUIOperation,
			.textColored = &UnsupportedUIOperation,
			.textDisabled = &UnsupportedUIOperation,
			.textWrapped = &UnsupportedUIOperation,
			.unindent = &UnsupportedUIOperation,
			.newLine = &UnsupportedUIOperation,
			.plotLines = &UnsupportedUIOperation
		};
		const DMUI_UIAPI* s_mockUI{ &kMockUI };
		DMUI_HostServices s_mockHostServices{
			DMUI_HOST_SERVICE_EXTERNAL_OPEN |
			DMUI_HOST_SERVICE_NAVIGATION_ICONS
		};

		inline constexpr std::initializer_list<ExpectedLogLevel> kExpectedLogLevels{
			{ LogControl::Level::kTrace, "trace"sv },
			{ LogControl::Level::kDebug, "debug"sv },
			{ LogControl::Level::kInfo, "info"sv },
			{ LogControl::Level::kWarn, "warn"sv },
			{ LogControl::Level::kError, "error"sv },
			{ LogControl::Level::kCritical, "critical"sv },
			{ LogControl::Level::kOff, "off"sv }
		};

		static_assert(kMenuMinRefreshMs == 100);
		static_assert(kMenuMaxRefreshMs == 2000);
		static_assert(ClampMenuFormattedLength(53, 48) == 47);
	}

	void run_menu_checks(Runner& runner)
	{
		runner.test("shared presentation styles preserve menu typography", [] {
			require(
				kHeadingText.fontRole == DMUI_FONT_ROLE_HEADING &&
					kHeadingText.tone == dmui::TextTone::kAccent,
				"section text lost its heading font or accent tone");
			require(
				kBodyText.fontRole == DMUI_FONT_ROLE_BODY &&
					kBodyText.tone == dmui::TextTone::kInherit,
				"diagnostic values no longer use ordinary body text");
			require(
				kMutedText.fontRole == DMUI_FONT_ROLE_SUBTEXT &&
					kMutedText.tone == dmui::TextTone::kMuted &&
					!kMutedText.wrapped,
				"muted descriptions changed typography or wrapping");
			require(
				(kDiagnosticTableFlags & dmui::ui::TableFlags::kSortable) == dmui::ui::TableFlags::kNone &&
					(kDiagnosticTableFlags & dmui::ui::TableFlags::kScrollY) != dmui::ui::TableFlags::kNone &&
					(kStaticDiagnosticTableFlags & dmui::ui::TableFlags::kScrollY) == dmui::ui::TableFlags::kNone,
				"diagnostic tables acquired sorting or lost scrolling");
		});

		runner.test("menu navigation preserves automatic General and explicit branding", [] {
			require(
				std::string_view{ kClientIconName } == "pill" &&
					DearModdingUI::FindPhosphorIconGlyphOrZero(kClientIconName) != 0,
				"client navigation branding lost its explicit pill glyph");
			require(
				std::string_view{ kGeneralCategory.id } == "general" &&
					std::string_view{ kGeneralCategory.displayName } == "General",
				"General lost its stable ID or display name");
			require(
				std::string_view{ kDiagnosticsCategory.id } == "diagnostics" &&
					std::string_view{ kDiagnosticsCategory.displayName } == "Diagnostics",
				"Diagnostics lost its stable ID or display name");
			require(
				kGeneralCategory.sortKey < kDiagnosticsCategory.sortKey,
				"General no longer sorts before Diagnostics");
			require(
				kGeneralCategory.iconName == nullptr,
				"General overrides the host's automatic semantic icon");
			require(
				kDiagnosticsCategory.iconName &&
					DearModdingUI::FindPhosphorIconGlyphOrZero(
						kDiagnosticsCategory.iconName) != 0,
				"Diagnostics has no valid explicit icon");

			const std::array pages{
				kHomePage,
				kSettingsPage,
				kModulesPage,
				kFacegenExceptionsPage,
				kLogControlPage
			};
			for (const auto& page : pages)
			{
				require(page.id && page.displayName && page.categoryId && page.summary,
					"menu page descriptor lost required metadata");
				require(
					page.iconName &&
						DearModdingUI::FindPhosphorIconGlyphOrZero(page.iconName) != 0,
					"menu page has no valid explicit icon");
				require(
					page.kind == DMUI_PAGE_KIND_SETTINGS,
					"menu page kind is not the supported navigable-page kind");
			}
			const Panel forwarded{
				kModulesPage,
				nullptr,
				nullptr,
				nullptr
			};
			require(
				std::string_view{ forwarded.page.iconName } ==
					std::string_view{ kModulesPage.iconName },
				"panel registration stopped forwarding the native page descriptor");
		});

		runner.test("telemetry navigation forwards explicit canonical icons", [] {
			require(
				kTelemetryPanels.size() ==
					static_cast<size_t>(TelemetryPanel::kCount),
				"telemetry page count no longer matches the public panel enum");
			for (const auto& panel : kTelemetryPanels)
			{
				require(
					panel.page.id && panel.page.displayName &&
						panel.page.categoryId && panel.page.summary,
					"telemetry page descriptor lost required metadata");
				require(
					std::string_view{ panel.page.categoryId } ==
						kDiagnosticsCategory.id,
					"telemetry page left the Diagnostics category");
				require(
					panel.page.kind == DMUI_PAGE_KIND_SETTINGS,
					"telemetry page kind is not the supported navigable-page kind");
				require(
					panel.page.iconName &&
						DearModdingUI::FindPhosphorIconGlyphOrZero(
							panel.page.iconName) != 0,
					"telemetry page has no valid explicit icon");
			}
		});

		runner.test("settings groups own explicit canonical icons", [] {
			for (const auto category : kSettingDisplayCategoryOrder)
			{
				const auto iconName =
					SettingDisplayCategoryIconName(category);
				require(!iconName.empty(),
					"settings display category has no explicit icon name");
				require(
					DearModdingUI::FindPhosphorIconGlyphOrZero(iconName) != 0,
					"settings display category icon is not in the canonical catalog");
			}
			require(
				SettingDisplayCategoryIconName(
					SettingDisplayCategory::kCount).empty(),
				"invalid settings display category acquired a fallback icon");
		});

		runner.test("home project links use host browser actions and icons", [] {
			require(kHomeQuickLinks.size() == 2, "project link count changed");
			require(
				std::string_view{ kHomeQuickLinks[0].external.target } ==
					"https://www.nexusmods.com/fallout4/mods/84214",
				"Nexus Mods link changed");
			require(
				std::string_view{ kHomeQuickLinks[1].external.target } ==
					"https://github.com/Dear-Modding-FO4/Addictol",
				"GitHub repository link changed");
			for (const auto& link : kHomeQuickLinks)
			{
				require(link.enabled, "project link is disabled");
				require(
					link.action == dmui::LinkAction::kOpenExternal &&
						link.external.targetKind == DMUI_EXTERNAL_TARGET_URI,
					"project link does not open its URI through the host");
				require(
					!link.external.application && link.external.arguments.empty() &&
						!link.external.workingDirectory,
					"project link overrides the default browser");
				require(link.glyph != 0, "project link icon is unavailable");
			}
			require(
				kHomeQuickLinks[1].glyph ==
					DearModdingUI::FindPhosphorIconGlyphOrZero("github-logo"),
				"GitHub link lost its logo");
		});

		runner.test("menu preflight requires external actions and navigation icons", [] {
			DMUI_HostAPI api{};
			api.structSize = sizeof(api);
			api.hostAbiVersion = DMUI_HOST_ABI_CURRENT;
			api.apiVersion = DMUI_API_VERSION_CURRENT;
			api.registerClient = [](
				const DMUI_ClientDescriptor*, DMUI_ClientHandle*) noexcept {
				return DMUI_RESULT_OK;
			};
			api.queryServices = [](DMUI_HostServicesInfo* a_services) noexcept {
				a_services->supportedServices = s_mockHostServices;
				return DMUI_RESULT_OK;
			};
			api.queryUIAPI = [](
				uint32_t abi, uint32_t revision, uint32_t tableSize,
				DMUI_UIAPIInfo* info) noexcept -> DMUI_Result {
				if (!info || info->structSize < DMUI_UI_API_INFO_1_SIZE)
					return DMUI_RESULT_STRUCT_TOO_SMALL;
				if (abi != s_mockUI->abiVersion ||
					revision > s_mockUI->revision ||
					tableSize > s_mockUI->structSize)
					return DMUI_RESULT_UNSUPPORTED_ABI;
				info->abiVersion = s_mockUI->abiVersion;
				info->revision = s_mockUI->revision;
				info->tableSize = s_mockUI->structSize;
				info->api = s_mockUI;
				return DMUI_RESULT_OK;
			};
			api.openExternal = [](
				DMUI_ClientHandle, const DMUI_ExternalOpenDescriptor*, uint32_t*) noexcept {
				return DMUI_RESULT_OK;
			};
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host without navigation registration entries passed preflight");
			api.registerPage = [](
				DMUI_ClientHandle, const DMUI_PageDescriptor*,
				DMUI_PageHandle* a_page) noexcept {
				if (a_page)
					*a_page = 1;
				return DMUI_RESULT_OK;
			};
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host without category registration passed preflight");
			api.registerCategory = [](
				DMUI_ClientHandle, const DMUI_CategoryDescriptor*) noexcept {
				return DMUI_RESULT_OK;
			};
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) == DMUI_RESULT_OK,
				"host with external actions and navigation icons failed preflight");

			const auto openExternal = api.openExternal;
			api.openExternal = nullptr;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host missing openExternal passed service preflight");
			api.openExternal = openExternal;
			s_mockHostServices = DMUI_HOST_SERVICE_EXTERNAL_OPEN;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host missing the navigation-icons service bit passed preflight");
			s_mockHostServices =
				DMUI_HOST_SERVICE_EXTERNAL_OPEN |
				DMUI_HOST_SERVICE_NAVIGATION_ICONS;

			const auto registerPage = api.registerPage;
			api.registerPage = nullptr;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host missing registerPage passed navigation preflight");
			api.registerPage = registerPage;
			const auto registerCategory = api.registerCategory;
			api.registerCategory = nullptr;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_SERVICE_UNAVAILABLE,
				"host missing registerCategory passed navigation preflight");
			api.registerCategory = registerCategory;

			DMUI_UIAPI missingPlot = kMockUI;
			missingPlot.plotLines = nullptr;
			s_mockUI = &missingPlot;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_UNSUPPORTED_ABI,
				"UI table missing the required plot operation passed preflight");
			DMUI_UIAPI truncatedUI = kMockUI;
			truncatedUI.structSize = DMUI_UI_API_PLOT_LINES_SIZE - 1;
			s_mockUI = &truncatedUI;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_UNSUPPORTED_ABI,
				"truncated UI table passed preflight");
			s_mockUI = &kMockUI;

			api.structSize = DMUI_HOST_API_UPDATE_IMAGE_SIZE;
			require(
				dmui::PreflightHostAPI(&api, kClientOptions) ==
					DMUI_RESULT_UNSUPPORTED_ABI,
				"truncated host API passed preflight");
			api.structSize = sizeof(api);
		});

		runner.test("input switching preserves modal keyboard and mouse ownership", [] {
			require(inputSwitchDetail::ShouldClearKeyboardMouseIgnore(false),
				"a closed menu prevented keyboard and mouse re-enabling");
			require(!inputSwitchDetail::ShouldClearKeyboardMouseIgnore(true),
				"an open menu allowed keyboard and mouse re-enabling");
		});

		runner.test("log level combo matches the public levels", [] {
			require(kMenuLogLevels.size() == kExpectedLogLevels.size(), "log level count changed");
			size_t index = 0;
			for (const auto& expected : kExpectedLogLevels)
			{
				const auto actual = kMenuLogLevels[index++];
				require(actual == expected.level, "log level order changed");
				require(LogControl::LevelName(actual) == expected.name, "public log level name changed");
			}
		});

		runner.test("module outcome choices have stable unique keys", [] {
			for (size_t index = 0; index < kModuleOutcomeFilters.size(); ++index)
			{
				const auto& option = kModuleOutcomeFilters[index];
				require(!option.label.empty(), "module outcome choice lost its label");
				require(!option.key.empty(), "module outcome choice lost its stable key");
				for (size_t other = index + 1;
					other < kModuleOutcomeFilters.size();
					++other)
				{
					require(
						option.key != kModuleOutcomeFilters[other].key,
						"module outcome choice keys are not unique");
				}
			}
		});

		runner.test("refresh interval clamps to its documented range", [] {
			require(ClampMenuRefreshMs(0) == 100, "zero did not clamp up");
			require(ClampMenuRefreshMs(100) == 100, "lower bound moved");
			require(ClampMenuRefreshMs(250) == 250, "in-range value changed");
			require(ClampMenuRefreshMs(2000) == 2000, "upper bound moved");
			require(ClampMenuRefreshMs(5000) == 2000, "above range did not clamp down");
		});

		runner.test("formatted text length never exceeds its buffer", [] {
			require(ClampMenuFormattedLength(-1, 48) == 0, "format failure returned a length");
			require(ClampMenuFormattedLength(20, 48) == 20, "short output was truncated");
			require(ClampMenuFormattedLength(48, 48) == 47, "terminator was counted as text");
			require(ClampMenuFormattedLength(80, 48) == 47, "long output escaped the buffer");
			require(ClampMenuFormattedLength(1, 0) == 0, "zero capacity returned a length");
		});

		runner.test("facegen exception values split and trim plugin-qualified fields", [] {
			const auto qualified = ParseFacegenExceptionValue(
				" 0x6e5b : DLCCoast.esm ");
			require(qualified.formID == "0x6e5b", "qualified FormID was not trimmed");
			require(
				qualified.pluginName == "DLCCoast.esm",
				"qualified plugin name was not trimmed");

			const auto bare = ParseFacegenExceptionValue("50359899");
			require(bare.formID == "50359899", "bare FormID changed");
			require(!bare.pluginName.has_value(), "bare FormID acquired a plugin name");

			const auto missing = ParseFacegenExceptionValue("0x1234: \t");
			require(
				missing.pluginName.has_value() && missing.pluginName->empty(),
				"missing plugin name was not retained");
		});

		runner.test("facegen FormIDs parse as hexadecimal or decimal", [] {
			require(ParseFacegenFormID("0x6e5b") == 0x6E5B, "hexadecimal FormID changed");
			require(ParseFacegenFormID("50359899") == 50359899, "decimal FormID changed");
		});

		runner.test("facegen exception entries serialize in INI form", [] {
			const FacegenExceptionDraft entry{
				" OldLongfellow ",
				" 0x6e5b ",
				" DLCCoast.esm "
			};
			require(
				SerializeFacegenExceptionEntry(entry) ==
					"OldLongfellow=0x6e5b:DLCCoast.esm",
				"qualified exception serialization changed");
			require(
				SerializeFacegenExceptionEntry({ "Marcy", "0x19FDC", std::nullopt }) ==
					"Marcy=0x19FDC",
				"bare exception serialization changed");
		});

		runner.test("facegen exception duplicate keys follow INI casing", [] {
			const std::vector<FacegenExceptionDraft> entries{
				{ "OldLongfellow", "0x6e5b", "DLCCoast.esm" },
				{ "Marcy", "0x19FDC", std::nullopt }
			};
			require(
				HasDuplicateFacegenExceptionKey(entries, "oldlongfellow"),
				"case-insensitive duplicate key was accepted");
			require(
				!HasDuplicateFacegenExceptionKey(entries, "oldlongfellow", 0),
				"edited key matched itself");
		});

		runner.test("facegen exception validation rejects invalid fields", [] {
			const std::vector<FacegenExceptionDraft> entries{
				{ "Existing", "0x1234", std::nullopt }
			};
			require(
				ValidateFacegenExceptionFields({ "", "0x1234", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kEmptyKey,
				"empty exception key was accepted");
			require(
				ValidateFacegenExceptionFields({ "Broken=Key", "0x1234", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kMalformedKey,
				"malformed exception key was accepted");
			require(
				ValidateFacegenExceptionFields({ "existing", "0x1234", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kDuplicateKey,
				"duplicate exception key was accepted");
			require(
				ValidateFacegenExceptionFields({ "New", "", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kEmptyFormID,
				"empty FormID was accepted");
			require(
				ValidateFacegenExceptionFields({ "New", "0x12ZZ", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kMalformedFormID,
				"malformed hexadecimal FormID was accepted");
			require(
				ValidateFacegenExceptionFields({ "New", "4294967296", std::nullopt }, entries).issue ==
					FacegenExceptionValidationIssue::kMalformedFormID,
				"out-of-range decimal FormID was accepted");
		});

		runner.test("SimpleIni preserves the shipped facegen documentation", [] {
			const std::filesystem::path source{
				"data/F4SE/Plugins/Addictol_FacegenExceptions.ini"
			};
			const std::filesystem::path output{
				".Build/Tests/facegen-exceptions-roundtrip.ini"
			};
			CSimpleIniA ini;
			require(ini.LoadFile(source.string().c_str()) == SI_OK, "shipped exceptions INI did not load");
			ini.SetSpaces(false);
			std::ifstream sourceFile{ source, std::ios::binary };
			const std::string sourceContents{
				std::istreambuf_iterator<char>{ sourceFile },
				std::istreambuf_iterator<char>{}
			};
			const auto leadingComments =
				ExtractFacegenExceptionLeadingComments(sourceContents);
			require(
				ini.SetValue(
					"FacegenException",
					"RoundTripProof",
					"0x6e5b:DLCCoast.esm",
					leadingComments.c_str()) >= SI_OK,
				"round-trip entry was not added");
			require(ini.SaveFile(output.string().c_str()) == SI_OK, "round-trip exceptions INI did not save");

			std::ifstream savedFile{ output, std::ios::binary };
			const std::string saved{
				std::istreambuf_iterator<char>{ savedFile },
				std::istreambuf_iterator<char>{}
			};
			const auto commentLines = [](std::string_view a_text) {
				std::vector<std::string> comments;
				size_t position = 0;
				while (position < a_text.size())
				{
					auto end = a_text.find_first_of("\r\n", position);
					if (end == std::string_view::npos)
						end = a_text.size();
					const auto line = a_text.substr(position, end - position);
					const auto first = line.find_first_not_of(" \t");
					if (first != std::string_view::npos &&
						(line[first] == ';' || line[first] == '#'))
						comments.emplace_back(line.substr(first));
					position = end;
					while (position < a_text.size() &&
						(a_text[position] == '\r' || a_text[position] == '\n'))
						++position;
				}
				return comments;
			};
			require(
				commentLines(saved) == commentLines(sourceContents),
				"documentation comments changed during the round trip");
			require(
				saved.contains("; ========== Addictol's FaceGen Exceptions list"),
				"documentation title was lost");
			require(
				saved.contains("; - <UniqueName> HAS TO BE UNIQUE FOR THIS LIST."),
				"documentation rules were lost");
			require(
				saved.contains("; OldLongfellow=0x3006e5b (or 0x6e5b:DLCCoast.esm)"),
				"inline examples were lost");
			require(
				saved.contains("RoundTripProof=0x6e5b:DLCCoast.esm"),
				"round-trip entry was lost");
			std::error_code error;
			(void)std::filesystem::remove(output, error);
		});

		runner.test("an open page refreshes once per cadence", [] {
			constexpr uint64_t frequency = 10'000'000;
			constexpr uint64_t last = frequency;
			require(ShouldRefreshPanel(false, last, last, frequency, 250), "first draw did not refresh");
			require(!ShouldRefreshPanel(true, last + frequency / 100, last, frequency, 250),
				"10 ms refreshed a 250 ms cadence");
			require(ShouldRefreshPanel(true, last + frequency / 4, last, frequency, 250),
				"250 ms did not refresh");
			require(!ShouldRefreshPanel(true, last, last, frequency, 250),
				"an unchanged counter refreshed");
		});

		runner.test("module outcomes classify into actionable severities", [] {
			require(
				ClassifyModuleOutcome(ModuleOutcome::kPending).severity ==
					ModuleOutcomeSeverity::kInfo,
				"pending outcome severity changed");
			require(
				ClassifyModuleOutcome(ModuleOutcome::kInstalled).severity ==
					ModuleOutcomeSeverity::kNormal,
				"installed outcome was not normal");
			require(
				ClassifyModuleOutcome(ModuleOutcome::kDisabled).severity ==
					ModuleOutcomeSeverity::kDisabled,
				"disabled outcome severity changed");
			require(
				ClassifyModuleOutcome(ModuleOutcome::kSkipped).severity ==
					ModuleOutcomeSeverity::kWarning,
				"skipped outcome was not a warning");
			require(
				ClassifyModuleOutcome(ModuleOutcome::kFailedQuery).severity ==
					ModuleOutcomeSeverity::kError,
				"failed query was not an error");
			require(
				ClassifyModuleOutcome(ModuleOutcome::kFailedInstall).severity ==
					ModuleOutcomeSeverity::kError,
				"failed install was not an error");
		});

		runner.test("module search and outcome filters combine deterministically", [] {
			require(
				MatchesModuleStatus(
					"Moon Rotation",
					ModuleOutcome::kInstalled,
					"roTAtion",
					ModuleOutcomeFilter::kAll),
				"case-insensitive module search did not match");
			require(
				!MatchesModuleStatus(
					"Moon Rotation",
					ModuleOutcome::kInstalled,
					"allocator",
					ModuleOutcomeFilter::kAll),
				"no-match module search was accepted");
			require(
				MatchesModuleStatus(
					"Scaleform Allocator",
					ModuleOutcome::kDisabled,
					"scale",
					ModuleOutcomeFilter::kDisabled),
				"combined module search and filter did not match");
			require(
				!MatchesModuleStatus(
					"Scaleform Allocator",
					ModuleOutcome::kDisabled,
					"",
					ModuleOutcomeFilter::kInstalled),
				"outcome filter accepted another outcome");
		});

		runner.test("per-module outcome tally equals ModuleOutcomeCounts", [] {
			std::array statuses{
				OutcomeStatus{ ModuleOutcome::kPending },
				OutcomeStatus{ ModuleOutcome::kPending },
				OutcomeStatus{ ModuleOutcome::kPending },
				OutcomeStatus{ ModuleOutcome::kPending },
				OutcomeStatus{ ModuleOutcome::kPending },
				OutcomeStatus{ ModuleOutcome::kPending }
			};
			ModuleOutcomeTally moduleOutcomeCounts{};
			RecordModuleOutcome(
				statuses[0].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kInstalled);
			RecordModuleOutcome(
				statuses[1].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kDisabled);
			RecordModuleOutcome(
				statuses[2].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kSkipped);
			RecordModuleOutcome(
				statuses[3].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kFailedInstall);
			RecordModuleOutcome(
				statuses[4].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kInstalled);
			RecordModuleOutcome(
				statuses[0].outcome,
				moduleOutcomeCounts,
				ModuleOutcome::kFailedQuery);

			require(
				TallyModuleOutcomes(statuses) == moduleOutcomeCounts,
				"per-module outcomes disagreed with ModuleOutcomeCounts");
		});

		runner.test("telemetry page definitions have stable identities and order", [] {
			constexpr std::array expectedIds{
				"overview"sv, "memory"sv, "decompression"sv, "stability"sv, "audio"sv
			};
			for (size_t index = 0; index < kTelemetryPanels.size(); ++index)
			{
				require(
					kTelemetryPanels[index].page.id == expectedIds[index],
					"telemetry page ID changed");
				require(
					kTelemetryPanels[index].page.sortKey ==
						static_cast<int32_t>(index * 10),
					"telemetry page sort order changed");
			}
		});

		runner.test("telemetry byte and percent values are formatted for display", [] {
			const auto bytes = FormatTelemetryValue({ 1536.0 * 1024.0, true }, Unit::kBytes);
			require(bytes.Text() == "1.50 MiB", "telemetry bytes were not scaled");
			const auto percent = FormatTelemetryValue({ 87.5, true }, Unit::kPercent);
			require(percent.Text() == "87.50%", "telemetry percent text changed");
			require(percent.progress && percent.fraction == 0.875f, "percent progress changed");
			require(TelemetryPercentFraction(125.0) == 1.0f, "percent exceeded its range");
		});

		runner.test("invalid telemetry values stay visually distinct", [] {
			const auto display = FormatTelemetryValue({ 42.0, false }, Unit::kCount);
			require(!display.valid, "invalid telemetry value became valid");
			require(display.Text() == "-", "invalid telemetry placeholder changed");
		});
	}
}
