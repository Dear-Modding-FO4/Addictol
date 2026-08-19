#include "../Addictol/Include/AdMenuTargets.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include "Harness.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol;

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

		// Transcribed independently of the header so a silent edit to either side fails the check.
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
		static_assert(kMenuPanelCapacity == 16);
		static_assert(ParseMenuToggleKey("F11"sv).virtualKey == 0x7A);
		static_assert(ParseMenuToggleKey("F11"sv).recognized);
		static_assert(!ParseMenuToggleKey("Q"sv).recognized);
		static_assert(ParseMenuToggleKey("Q"sv).virtualKey == 0x7A);
		static_assert(ClampMenuFormattedLength(53, 48) == 47);

		// Stands in for REX::TOML::Bool<>, which the test target cannot see.
		struct FakeGate
		{
			bool value{ false };

			[[nodiscard]] bool GetValue() const noexcept { return value; }
		};

		using Panels = MenuPanelTable<FakeGate>;

		std::vector<std::string> drawn;
		std::vector<std::string> offered;
		const void* drawnContext{ nullptr };

		void draw_a(const void* a_context) noexcept
		{
			drawn.emplace_back("a");
			drawnContext = a_context;
		}
		void draw_b(const void* a_context) noexcept
		{
			drawn.emplace_back("b");
			drawnContext = a_context;
		}
		void draw_c(const void* a_context) noexcept
		{
			drawn.emplace_back("c");
			drawnContext = a_context;
		}
		void draw_log(const void* a_context) noexcept
		{
			drawn.emplace_back("log");
			drawnContext = a_context;
		}

		// Reports activation only; the decision to call a panel belongs to DrawPanels.
		[[nodiscard]] auto activator(std::string_view a_active)
		{
			return [a_active](const char* a_name) noexcept {
				offered.emplace_back(a_name);
				return std::string_view{ a_name } == a_active;
			};
		}

		void end_panel() noexcept {}
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
				require(
					MenuToggleKeyName(parsed.virtualKey) == key.name,
					"virtual key did not round trip to its name");
			}
		});

		runner.test("toggle key parser ignores case", [] {
			require(ParseMenuToggleKey("f11"sv).virtualKey == 0x7A, "lower case name rejected");
			require(ParseMenuToggleKey("hOmE"sv).virtualKey == 0x24, "mixed case name rejected");
			require(ParseMenuToggleKey("DELETE"sv).virtualKey == 0x2E, "upper case name rejected");
		});

		runner.test("unsupported toggle keys fall back to F11", [] {
			for (const auto name : { ""sv, "F0"sv, "F13"sv, "Escape"sv, "PageUp"sv, "F1 "sv }) {
				const auto parsed = ParseMenuToggleKey(name);
				require(!parsed.recognized, "unsupported name was accepted");
				require(parsed.virtualKey == 0x7A, "fallback is not F11");
			}
		});

		runner.test("refresh interval clamps to its documented range", [] {
			require(ClampMenuRefreshMs(0) == 100, "zero did not clamp up");
			require(ClampMenuRefreshMs(99) == 100, "below range did not clamp up");
			require(ClampMenuRefreshMs(100) == 100, "lower bound moved");
			require(ClampMenuRefreshMs(250) == 250, "in-range value changed");
			require(ClampMenuRefreshMs(2000) == 2000, "upper bound moved");
			require(ClampMenuRefreshMs(5000) == 2000, "above range did not clamp down");
		});

		runner.test("formatted text length never exceeds its buffer", [] {
			require(ClampMenuFormattedLength(-1, 48) == 0, "format failure returned a length");
			require(ClampMenuFormattedLength(0, 48) == 0, "empty output returned a length");
			require(ClampMenuFormattedLength(20, 48) == 20, "short output was truncated");
			require(ClampMenuFormattedLength(47, 48) == 47, "exact output was truncated");
			require(ClampMenuFormattedLength(48, 48) == 47, "terminator was counted as text");
			require(ClampMenuFormattedLength(80, 48) == 47, "long output escaped the buffer");
			require(ClampMenuFormattedLength(1, 0) == 0, "zero-capacity output returned a length");
		});

		runner.test("an open panel refreshes once per cadence", [] {
			constexpr uint64_t frequency = 10'000'000;
			constexpr uint64_t last = frequency;
			require(
				ShouldRefreshPanel(false, last, last, frequency, 250),
				"the first draw did not populate the cache");
			require(
				!ShouldRefreshPanel(true, last + frequency / 100, last, frequency, 250),
				"10 ms was enough to refresh a 250 ms cadence");
			require(
				!ShouldRefreshPanel(true, last + (frequency * 249) / 1000, last, frequency, 250),
				"249 ms was enough to refresh a 250 ms cadence");
			require(
				ShouldRefreshPanel(true, last + frequency / 4, last, frequency, 250),
				"250 ms did not refresh");
			require(
				!ShouldRefreshPanel(true, last, last, frequency, 250),
				"an unchanged counter refreshed");
			require(
				!ShouldRefreshPanel(true, last + (frequency * 75) / 1000, last, frequency, 50),
				"an out-of-range cadence was not clamped to 100 ms");
			require(
				ShouldRefreshPanel(true, last + frequency * 3, last, frequency, 5000),
				"an out-of-range cadence was not clamped to 2000 ms");
		});

		runner.test("menu toggle decisions follow the key and the platform", [] {
			constexpr uint32_t toggle = 0x7A;

			auto decision = DecideMenuToggle(0x70, toggle, false, true);
			require(!decision.matched, "a key that is not the toggle key changes nothing");
			require(!decision.open, "a foreign key opened a closed menu");
			decision = DecideMenuToggle(0x70, toggle, true, true);
			require(!decision.matched, "a key that is not the toggle key changes nothing");
			require(decision.open, "a foreign key closed an open menu");

			decision = DecideMenuToggle(toggle, toggle, false, true);
			require(decision.matched && decision.open, "the toggle key did not open a closed menu");
			decision = DecideMenuToggle(toggle, toggle, true, true);
			require(decision.matched && !decision.open, "the toggle key did not close an open menu");
			decision = DecideMenuToggle(toggle, toggle, true, false);
			require(decision.matched && decision.open,
				"an open menu follows the platform disabling drawing");
			decision = DecideMenuToggle(toggle, toggle, false, false);
			require(decision.matched && decision.open,
				"a closed menu did not reopen while drawing was disabled");
		});

		runner.test("panel registration rejects null draw, duplicates and overlong names", [] {
			Panels panels;
			require(panels.IsOpen(), "a fresh registry accepts panels");
			require(panels.Empty(), "a fresh registry holds no panels");

			require(panels.Add("Overview"sv, &draw_a, nullptr) == Registration::kAccepted,
				"the first panel is accepted");
			require(panels.Size() == 1, "an accepted panel is counted");
			require(panels.Name(0) == "Overview", "the name is copied into the registry");
			require(panels.At(0).draw == &draw_a, "the accepted draw callback is retrievable");

			require(panels.Add("Log Control"sv, nullptr, nullptr) == Registration::kNullCallback,
				"a panel without a draw callback is rejected");
			require(panels.Add("Overview"sv, &draw_b, nullptr) == Registration::kDuplicate,
				"a panel name is registered only once");
			require(panels.Add(""sv, &draw_b, nullptr) == Registration::kInvalidName,
				"an empty panel name is rejected");
			require(panels.Add(std::string(kNameCapacity, 'x'), &draw_b, nullptr) == Registration::kInvalidName,
				"a panel name that does not fit is rejected");
			require(panels.Size() == 1, "a rejected panel changes nothing");
			require(panels.At(0).draw == &draw_a, "a rejected panel leaves the registry untouched");

			require(panels.Add(std::string(kNameCapacity - 1, 'x'), &draw_b, nullptr) == Registration::kAccepted,
				"the longest fitting panel name is accepted");
			require(panels.Name(1).size() == kNameCapacity - 1, "the longest panel name round trips");
		});

		runner.test("panel registration rejects overflow", [] {
			Panels panels;
			for (size_t index = 0; index < panels.MaxSize(); ++index)
			{
				require(panels.Add("panel" + std::to_string(index), &draw_a, nullptr) == Registration::kAccepted,
					"capacity must hold " + std::to_string(panels.MaxSize()) + " panels");
			}
			require(panels.Size() == panels.MaxSize(), "the registry is full");
			require(panels.Add("overflow"sv, &draw_a, nullptr) == Registration::kFull,
				"panel registration stops at capacity");
		});

		runner.test("telemetry panel set fits the menu registry", [] {
			Panels panels;
			for (const auto& panel : kTelemetryPanels)
			{
				require(
					panels.Add(panel.name, &draw_a, nullptr, &panel) ==
						Registration::kAccepted,
					"telemetry panel registration exceeded capacity");
			}
			require(
				panels.Add("Log Control"sv, &draw_log, nullptr) ==
					Registration::kAccepted,
				"telemetry panels left no room for log control");
			require(panels.Size() == 6, "telemetry panel registration count changed");
		});

		runner.test("telemetry byte and percent values are formatted for display", [] {
			const auto bytes = FormatTelemetryValue(
				{ 1536.0 * 1024.0, true },
				Unit::kBytes);
			require(bytes.Text() == "1.50 MiB", "telemetry bytes were not scaled to MiB");

			const auto percent = FormatTelemetryValue({ 87.5, true }, Unit::kPercent);
			require(percent.Text() == "87.50%", "telemetry percent text changed");
			require(percent.progress, "telemetry percent lost its progress visual");
			require(percent.fraction == 0.875f, "telemetry percent progress was not normalized");

			require(
				TelemetryPercentFraction(125.0) == 1.0f,
				"telemetry percent progress exceeded its range");
		});

		runner.test("invalid telemetry values stay visually distinct", [] {
			const auto display = FormatTelemetryValue({ 42.0, false }, Unit::kCount);
			require(
				!display.valid,
				"invalid telemetry value did not render as a placeholder");
			require(display.Text() == "-", "invalid telemetry placeholder text changed");
		});

		runner.test("a gated panel follows its option and an ungated panel is always shown", [] {
			Panels panels;
			FakeGate off{ false };
			FakeGate on{ true };
			require(panels.Add("Always"sv, &draw_a, nullptr) == Registration::kAccepted, "the ungated panel registers");
			require(panels.Add("Gated off"sv, &draw_b, &off) == Registration::kAccepted, "the closed gate registers");
			require(panels.Add("Gated on"sv, &draw_c, &on) == Registration::kAccepted, "the open gate registers");

			offered.clear();
			drawn.clear();
			DrawPanels(panels, activator("nothing"sv), &end_panel);
			require(offered == std::vector<std::string>{ "Always", "Gated on" },
				"a gated panel is hidden while its option is false");
			require(drawn.empty(), "a panel was drawn without being activated");

			off.value = true;
			offered.clear();
			DrawPanels(panels, activator("nothing"sv), &end_panel);
			require(offered == std::vector<std::string>{ "Always", "Gated off", "Gated on" },
				"a gated panel stayed hidden after its option turned true");
		});

		runner.test("only the active panel is drawn", [] {
			Panels panels;
			require(panels.Add("Overview"sv, &draw_a, nullptr) == Registration::kAccepted, "the first panel registers");
			int allocatorContext{ 42 };
			require(panels.Add("Allocator"sv, &draw_b, nullptr, &allocatorContext) == Registration::kAccepted,
				"the second panel registers");
			require(panels.Add("Memory"sv, &draw_c, nullptr) == Registration::kAccepted, "the third panel registers");

			offered.clear();
			drawn.clear();
			DrawPanels(panels, activator("Allocator"sv), &end_panel);
			require(offered == std::vector<std::string>{ "Overview", "Allocator", "Memory" },
				"panels are offered out of registration order");
			require(drawn == std::vector<std::string>{ "b" }, "only the active panel is drawn");
			require(drawnContext == &allocatorContext, "the active panel did not receive its context");

			offered.clear();
			drawn.clear();
			DrawPanels(panels, activator("Memory"sv), &end_panel);
			require(drawn == std::vector<std::string>{ "c" }, "only the active panel is drawn");
		});

		runner.test("finalization pins log control last and requests platform sinks", [] {
			Panels panels;
			FakeGate gate{ true };
			require(panels.Add("Overview"sv, &draw_a, &gate) == Registration::kAccepted, "the profiler panel registers");
			require(panels.Add("Allocator"sv, &draw_b, &gate) == Registration::kAccepted, "the second panel registers");

			bool requested{ false };
			const auto result = FinalizeMenuPanels(panels, "Log Control"sv, &draw_log, true, [&requested]() noexcept {
				requested = true;
			});

			require(result == Registration::kAccepted, "finalization did not accept the log control panel");
			require(panels.Size() == 3, "finalization did not append the log control panel");
			require(panels.Name(panels.Size() - 1) == "Log Control", "log control is the last panel");
			require(panels.At(panels.Size() - 1).gate == nullptr, "log control must not be gated");
			require(!panels.IsOpen(), "finalization did not close the panel registry");
			require(requested, "a requested menu did not ask the platform for its sinks");

			require(panels.Add("late"sv, &draw_c, nullptr) == Registration::kClosed,
				"a panel registered after finalization is rejected");
			require(panels.Size() == 3, "late registration changed the finalized panel table");
		});

		runner.test("a disabled menu requests no platform sinks", [] {
			Panels panels;
			FakeGate gate{ false };
			require(panels.Add("Overview"sv, &draw_a, &gate) == Registration::kAccepted,
				"panels are still contributed while the menu is disabled");

			bool requested{ false };
			const auto result = FinalizeMenuPanels(panels, "Log Control"sv, &draw_log, false, [&requested]() noexcept {
				requested = true;
			});

			require(result == Registration::kAccepted, "finalization did not accept the log control panel");
			require(!requested, "a disabled menu asked the platform for sinks");
			require(panels.Name(panels.Size() - 1) == "Log Control", "log control is the last panel");
			require(!panels.IsOpen(), "finalization did not close the panel registry");
		});

		// Log control is the reason the menu exists, so a menu that lost it must not reach the platform.
		runner.test("a menu that cannot register log control asks for no platform sinks", [] {
			Panels panels;
			for (size_t index = 0; index < 16; ++index)
			{
				require(panels.Add("panel" + std::to_string(index), &draw_a, nullptr) == Registration::kAccepted,
					"capacity must hold 16 contributed panels");
			}

			bool requested{ false };
			const auto result = FinalizeMenuPanels(panels, "Log Control"sv, &draw_log, true, [&requested]() noexcept {
				requested = true;
			});

			require(result == Registration::kFull, "a full registry did not reject log control");
			require(!requested, "a menu without log control asked the platform for sinks");
			require(!panels.IsOpen(), "a failed finalization did not close the panel registry");
			require(panels.Size() == 16, "a failed finalization changed the contributed panels");
			require(panels.Name(panels.Size() - 1) == "panel15", "a failed finalization reordered the contributed panels");
		});
	}
}
