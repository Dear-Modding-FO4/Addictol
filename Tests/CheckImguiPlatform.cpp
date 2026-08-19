#include "../Addictol/Include/Core/AdSignature.h"
#include "../Addictol/Include/Platform/AdImguiPlatformTargets.h"
#include "Harness.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace
{
	using namespace Addictol::ImguiPlatform;

	void draw_sink_a() noexcept {}
	void draw_sink_b() noexcept {}
	void key_sink(uint32_t) noexcept {}

	using DrawSink = void (*)() noexcept;
	using KeySink = void (*)(uint32_t) noexcept;

	// Pinned independently of the header so a silent edit to either side fails the check.
	constexpr std::optional<uint8_t> expected_og[]{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
		0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 0x65, 0x48,
		0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x3C, 0xD0, 0xB9, 0xC0, 0x09,
		0x00, 0x00 };
	constexpr std::optional<uint8_t> expected_ngandae[]{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
		0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41,
		0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 0x4C, 0x8B, 0xF9 };

	// A candidate of a different length is a mismatch: partial prologue agreement proves nothing.
	[[nodiscard]] constexpr bool MatchesSignature(
		std::ranges::contiguous_range auto&& a_candidate,
		const std::initializer_list<std::optional<uint8_t>>& a_signature) noexcept
	{
		return a_signature.size() != 0 && std::ranges::equal(a_candidate, a_signature);
	}

	void check_every_byte_mutation(vmm_tests::Runner& runner, std::string_view name, Runtime runtime)
	{
		runner.test(name, [runtime] {
			const auto& signature = UIEndFrameSignature(runtime);
			vmm_tests::require(MatchesSignature(signature, signature), "unmutated signature must match");

			std::vector<std::optional<uint8_t>> mutated(signature.begin(), signature.end());
			for (size_t index = 0; index < mutated.size(); ++index)
			{
				const auto original = mutated[index];
				if (!original)
					continue;

				for (const uint8_t delta : { uint8_t{ 1 }, uint8_t{ 0x80 }, uint8_t{ 0xFF } })
				{
					mutated[index] = static_cast<uint8_t>(*original ^ delta);
					if (mutated[index] == original)
						continue;
					vmm_tests::require(
						!MatchesSignature(mutated, signature),
						"byte " + std::to_string(index) + " must be load bearing");
				}
				mutated[index] = original;
			}

			const std::vector<std::optional<uint8_t>> truncated(signature.begin(), std::prev(signature.end()));
			vmm_tests::require(
				!MatchesSignature(truncated, signature),
				"a truncated candidate must not match");
		});
	}
}

namespace vmm_tests
{
	void run_imgui_platform_checks(Runner& runner)
	{
		runner.test("wildcard signature takes target bytes only at masked positions", [] {
			constexpr uint8_t target[]{ 0x48, 0x8B, 0x11, 0x22, 0x33, 0x44, 0x90 };
			constexpr std::optional<uint8_t> masked[]{
				0x48, 0x8B, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 0x90
			};
			const auto address = reinterpret_cast<uintptr_t>(target);
			const auto built = RELEX::GetWildcardSignature(address, masked);

			require(built.size() == std::size(masked), "wildcard signature must keep its length");
			require(built[0] == 0x48 && built[1] == 0x8B, "concrete bytes must survive");
			require(built[6] == 0x90, "trailing concrete byte must survive");
			require(built[2] == 0x11 && built[3] == 0x22 && built[4] == 0x33 && built[5] == 0x44,
				"masked positions must take the target's own bytes");
			require(RELEX::Validate(address, built), "a wildcard signature must match its target");

			uint8_t moved[std::size(target)]{};
			std::ranges::copy(target, std::begin(moved));
			moved[3] = 0x99;
			const auto relocated = RELEX::GetWildcardSignature(reinterpret_cast<uintptr_t>(moved), masked);
			require(RELEX::Validate(reinterpret_cast<uintptr_t>(moved), relocated),
				"a masked operand must not break validation when it changes");

			moved[1] = 0x8C;
			require(!RELEX::Validate(reinterpret_cast<uintptr_t>(moved), built),
				"a changed concrete byte must fail validation");
		});

		runner.test("wildcard signature rejects an empty mask", [] {
			constexpr uint8_t target[]{ 0x48 };
			const auto address = reinterpret_cast<uintptr_t>(target);
			require(RELEX::GetWildcardSignature(address, {}).empty(),
				"an empty mask must produce no signature");
			require(!RELEX::Validate(address, {}), "an empty signature must never validate");
		});

		runner.test("UIEndFrame ids are pinned per runtime", [] {
			require(kUIEndFrameId.og == 137303, "OG id must be 137303");
			require(kUIEndFrameId.ng == 2284763, "NG id must be 2284763");
			require(kUIEndFrameId.ae == 2284763, "AE id must be 2284763");
			require(UIEndFrameId(Runtime::kOG) == 137303, "OG lookup must resolve the OG id");
			require(UIEndFrameId(Runtime::kNG) == 2284763, "NG lookup must resolve the NG id");
			require(UIEndFrameId(Runtime::kAE) == 2284763, "AE lookup must resolve the AE id");
		});

		runner.test("UIEndFrame signature bytes are pinned per runtime", [] {
			require(MatchesSignature(expected_og, UIEndFrameSignature(Runtime::kOG)), "OG signature drifted");
			require(MatchesSignature(expected_ngandae, UIEndFrameSignature(Runtime::kNG)), "NG signature drifted");
			require(MatchesSignature(expected_ngandae, UIEndFrameSignature(Runtime::kAE)), "AE signature drifted");
			require(!MatchesSignature(expected_og, UIEndFrameSignature(Runtime::kNG)),
				"OG and NG signatures must not cross match");
			require(!MatchesSignature(std::initializer_list<uint8_t>{}, UIEndFrameSignature(Runtime::kOG)),
				"an empty candidate must not match");
			require(!MatchesSignature(expected_og, {}), "an empty signature must never validate");
		});

		check_every_byte_mutation(runner, "every OG signature byte is load bearing", Runtime::kOG);
		check_every_byte_mutation(runner, "every NG signature byte is load bearing", Runtime::kNG);
		check_every_byte_mutation(runner, "every AE signature byte is load bearing", Runtime::kAE);

		runner.test("install state governs registration, attempts and readiness", [] {
			require(AllowsInstallAttempt(InstallState::kNotAttempted), "the first attempt must be allowed");
			require(!AllowsInstallAttempt(InstallState::kRejected), "a rejected target is never retried");
			require(!AllowsInstallAttempt(InstallState::kAttempted), "a started attempt is never repeated");
			require(!AllowsInstallAttempt(InstallState::kInstalled), "installation is idempotent");
			require(!AllowsInstallAttempt(InstallState::kIndeterminate), "an indeterminate target is never retried");

			require(IsInstalled(InstallState::kInstalled), "only the installed state is ready");
			require(!IsInstalled(InstallState::kAttempted), "an attempt alone is not ready");
			require(!IsInstalled(InstallState::kIndeterminate), "an indeterminate target is not ready");
			require(Describe(InstallState::kIndeterminate) == "indeterminate", "state descriptions are user facing");
		});

		runner.test("sink registration rejects null, duplicate and overlong names", [] {
			SinkTable<DrawSink> table;
			require(table.IsOpen(), "a fresh table accepts registrations");
			require(table.Empty(), "a fresh table holds no sinks");

			require(table.Add("overview", draw_sink_a) == Registration::kAccepted, "the first sink is accepted");
			require(table.Size() == 1, "an accepted sink is counted");
			require(table.At(0) == draw_sink_a, "the accepted sink is retrievable");
			require(table.Name(0) == "overview", "the name is copied into the table");

			require(table.Add("overview", draw_sink_b) == Registration::kDuplicate, "a duplicate name is rejected");
			require(table.Add("other", nullptr) == Registration::kNullSink, "a null callback is rejected");
			require(table.Add("", draw_sink_b) == Registration::kInvalidName, "an empty name is rejected");
			require(table.Add(std::string(kSinkNameCapacity, 'x'), draw_sink_b) == Registration::kInvalidName,
				"a name that does not fit is rejected");
			require(table.Size() == 1, "a rejected registration changes nothing");

			require(table.Add(std::string(kSinkNameCapacity - 1, 'x'), draw_sink_b) == Registration::kAccepted,
				"the longest fitting name is accepted");
			require(table.Name(1).size() == kSinkNameCapacity - 1, "the longest name round trips");
		});

		runner.test("sink registration remains open for late arrivals and rejects overflow", [] {
			SinkTable<KeySink> table;
			require(table.Add("early", key_sink) == Registration::kAccepted,
				"the initial sink is accepted");
			require(table.IsOpen(), "an accepted sink did not leave registration open");
			require(table.Add("late", key_sink) == Registration::kAccepted,
				"a later sink is accepted while registration remains open");

			for (size_t index = table.Size(); index < table.MaxSize(); ++index)
			{
				require(table.Add("sink" + std::to_string(index), key_sink) == Registration::kAccepted,
					"capacity must hold " + std::to_string(table.MaxSize()) + " sinks");
			}
			require(table.Size() == table.MaxSize(), "the table is full");
			require(table.Add("overflow", key_sink) == Registration::kFull, "overflow is rejected");
		});

		runner.test("window messages are classified and swallowed by capture state", [] {
			require(ClassifyMessage(0x0100) == MessageClass::kKeyboard, "WM_KEYDOWN is keyboard");
			require(ClassifyMessage(0x0102) == MessageClass::kKeyboard, "WM_CHAR is keyboard");
			require(ClassifyMessage(0x0109) == MessageClass::kKeyboard, "the last keyboard message is keyboard");
			require(ClassifyMessage(0x0200) == MessageClass::kMouse, "WM_MOUSEMOVE is mouse");
			require(ClassifyMessage(0x020E) == MessageClass::kMouse, "WM_MOUSEHWHEEL is mouse");
			require(ClassifyMessage(0x00FF) == MessageClass::kOther, "WM_INPUT is neither");
			require(ClassifyMessage(0x0020) == MessageClass::kOther, "WM_SETCURSOR is neither");
			require(ClassifyMessage(0x010A) == MessageClass::kOther, "the message after the keyboard range is neither");
			require(ClassifyMessage(0x020F) == MessageClass::kOther, "the message after the mouse range is neither");

			require(SwallowsMessage(MessageClass::kMouse, true, false), "captured mouse input stops at the menu");
			require(!SwallowsMessage(MessageClass::kMouse, false, true), "uncaptured mouse input reaches the game");
			require(SwallowsMessage(MessageClass::kKeyboard, false, true), "captured keys stop at the menu");
			require(!SwallowsMessage(MessageClass::kKeyboard, true, false), "uncaptured keys reach the game");
			require(!SwallowsMessage(MessageClass::kOther, true, true), "other messages always reach the game");
		});

		runner.test("toggle sinks fire once per physical press", [] {
			require(IsKeyRepeat(kKeyRepeatBit), "bit 30 marks an auto repeat");
			require(!IsKeyRepeat(0), "a first press carries no repeat bit");
			require(!IsKeyRepeat(0x0001), "the repeat count does not mark a repeat");
			require(IsKeyRepeat(kKeyRepeatBit | 0xC0000001ull), "release flags do not hide the repeat bit");

			require(DispatchesToggleSinks(0x0100, 0x0001), "a fresh WM_KEYDOWN dispatches");
			require(!DispatchesToggleSinks(0x0100, kKeyRepeatBit | 0x0001), "a held key does not redispatch");
			require(!DispatchesToggleSinks(0x0101, 0x0001), "WM_KEYUP does not dispatch");
			require(DispatchesToggleSinks(0x0104, 0x0001),
				"bare F10 arrives as WM_SYSKEYDOWN and must dispatch");
			require(!DispatchesToggleSinks(0x0104, kKeyRepeatBit | 0x0001),
				"a held system key does not redispatch");
			require(!DispatchesToggleSinks(0x0105, 0x0001), "WM_SYSKEYUP does not dispatch");
			require(ClassifyMessage(0x0104) == MessageClass::kKeyboard,
				"WM_SYSKEYDOWN is keyboard traffic and follows the capture state");
		});
	}
}
