#include "../Addictol/Include/Menu/AdMenuTargets.h"
#include "../Addictol/Include/Menu/AdMenuModules.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include "Harness.h"

#include <initializer_list>
#include <string>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol;
		using namespace Addictol::Menu;

		struct ExpectedToggleKey
		{
			std::string_view name;
			uint32_t virtualKey;
		};

		struct ExpectedLogLevel
		{
			LogControl::Level level;
			std::string_view name;
		};

		struct OutcomeStatus
		{
			ModuleOutcome outcome;
		};

		inline constexpr std::initializer_list<ExpectedToggleKey> kExpectedToggleKeys{
			{ "F1"sv, 0x70 },
			{ "F2"sv, 0x71 },
			{ "F3"sv, 0x72 },
			{ "F4"sv, 0x73 },
			{ "F5"sv, 0x74 },
			{ "F6"sv, 0x75 },
			{ "F7"sv, 0x76 },
			{ "F8"sv, 0x77 },
			{ "F9"sv, 0x78 },
			{ "F10"sv, 0x79 },
			{ "F11"sv, 0x7A },
			{ "F12"sv, 0x7B },
			{ "Home"sv, 0x24 },
			{ "End"sv, 0x23 },
			{ "Insert"sv, 0x2D },
			{ "Delete"sv, 0x2E }
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

		static_assert(kMenuDefaultToggleKey == 0x7A);
		static_assert(kMenuMinRefreshMs == 100);
		static_assert(kMenuMaxRefreshMs == 2000);
		static_assert(ParseMenuToggleKey("F11"sv).virtualKey == 0x7A);
		static_assert(ParseMenuToggleKey("F11"sv).recognized);
		static_assert(!ParseMenuToggleKey("Q"sv).recognized);
		static_assert(ClampMenuFormattedLength(53, 48) == 47);
	}

	void run_menu_checks(Runner& runner)
	{
		runner.test("toggle key table matches pinned virtual keys", [] {
			require(kMenuToggleKeys.size() == kExpectedToggleKeys.size(), "toggle key count changed");
			size_t index = 0;
			for (const auto& expected : kExpectedToggleKeys)
			{
				const auto& actual = kMenuToggleKeys[index++];
				require(actual.name == expected.name, "toggle key name or order changed");
				require(actual.virtualKey == expected.virtualKey, "toggle virtual key changed");
			}
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

		runner.test("toggle key parser accepts every supported name", [] {
			for (const auto& key : kExpectedToggleKeys)
			{
				const auto parsed = ParseMenuToggleKey(key.name);
				require(parsed.recognized, std::string("rejected ") + std::string(key.name));
				require(parsed.virtualKey == key.virtualKey, "wrong virtual key");
				require(MenuToggleKeyName(parsed.virtualKey) == key.name, "toggle key did not round trip");
			}
		});

		runner.test("toggle key parser ignores case and rejects unsupported names", [] {
			require(ParseMenuToggleKey("f11"sv).virtualKey == 0x7A, "lower case name rejected");
			require(ParseMenuToggleKey("hOmE"sv).virtualKey == 0x24, "mixed case name rejected");
			for (const auto name : { ""sv, "F0"sv, "F13"sv, "Escape"sv, "F1 "sv })
			{
				const auto parsed = ParseMenuToggleKey(name);
				require(!parsed.recognized, "unsupported name was accepted");
				require(parsed.virtualKey == kMenuDefaultToggleKey, "fallback is not F11");
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

		runner.test("menu toggle decisions follow the key and backend readiness", [] {
			constexpr uint32_t toggle = 0x7A;
			auto decision = DecideMenuToggle(0x70, toggle, false, true);
			require(!decision.matched && !decision.open, "a foreign key opened the menu");
			decision = DecideMenuToggle(toggle, toggle, false, true);
			require(decision.matched && decision.open, "toggle did not open the menu");
			decision = DecideMenuToggle(toggle, toggle, true, true);
			require(decision.matched && !decision.open, "toggle did not close the menu");
			decision = DecideMenuToggle(toggle, toggle, true, false);
			require(decision.matched && decision.open, "backend loss changed the requested state");
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
					"Weapon Debris Crash",
					ModuleOutcome::kInstalled,
					"deBRis",
					ModuleOutcomeFilter::kAll),
				"case-insensitive module search did not match");
			require(
				!MatchesModuleStatus(
					"Weapon Debris Crash",
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
				require(kTelemetryPanels[index].id == expectedIds[index], "telemetry page ID changed");
				require(kTelemetryPanels[index].sortKey == static_cast<int32_t>(index * 10),
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
