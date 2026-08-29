#include "../Addictol/Include/Platform/AdImguiPlatformTargets.h"
#include "Harness.h"

#include <limits>
#include <string>

namespace
{
	using namespace Addictol::ImguiPlatform;

	void draw_sink_a() noexcept {}
	void draw_sink_b() noexcept {}
	bool key_sink(uint32_t) noexcept { return false; }

	using DrawSink = void (*)() noexcept;
	using KeySink = bool (*)(uint32_t) noexcept;
}

namespace vmm_tests
{
	void run_imgui_platform_checks(Runner& runner)
	{
		runner.test("swapchain vtable slots match the DXGI ABI", [] {
			require(kPresentSlot == 8, "Present must use slot 8");
			require(kResizeBuffersSlot == 13, "ResizeBuffers must use slot 13");
		});

		runner.test("swapchain discovery attaches once and explicit handoff retargets", [] {
			constexpr AttachmentIdentity empty{};
			constexpr AttachmentIdentity game{ 1, 2, 3, 4 };
			constexpr AttachmentIdentity sameGame{ 1, 2, 3, 4 };
			constexpr AttachmentIdentity reboundGame{ 1, 2, 3, 8 };
			constexpr AttachmentIdentity other{ 5, 2, 3, 4 };
			constexpr AttachmentIdentity invalid{ 5, 2, 0, 4 };

			require(
				DecideAttachment(
					empty,
					game,
					AttachmentSource::kDiscovery,
					AttachmentLifecycle::kVacant) ==
					AttachmentDecision::kAttach,
				"the first discovered swapchain must attach");
			require(
				DecideAttachment(
					game,
					sameGame,
					AttachmentSource::kExplicit,
					AttachmentLifecycle::kActive) ==
					AttachmentDecision::kKeepCurrent,
				"reattaching the same swapchain must be idempotent");
			require(
				DecideAttachment(
					game,
					reboundGame,
					AttachmentSource::kDiscovery,
					AttachmentLifecycle::kActive) ==
					AttachmentDecision::kAttach,
				"a captured swapchain with a changed binding must refresh its attachment");
			require(
				DecideAttachment(
					game,
					other,
					AttachmentSource::kDiscovery,
					AttachmentLifecycle::kActive) ==
					AttachmentDecision::kKeepCurrent,
				"an active attachment must keep an unrelated discovery");
			require(
				DecideAttachment(
					game,
					other,
					AttachmentSource::kDiscovery,
					AttachmentLifecycle::kRetired) ==
					AttachmentDecision::kAttach,
				"a retired attachment must accept a discovered replacement");
			require(
				DecideAttachment(
					game,
					sameGame,
					AttachmentSource::kDiscovery,
					AttachmentLifecycle::kRetired) ==
					AttachmentDecision::kAttach,
				"a retired address reused by a new swapchain must attach");
			require(
				DecideAttachment(
					game,
					other,
					AttachmentSource::kExplicit,
					AttachmentLifecycle::kActive) ==
					AttachmentDecision::kAttach,
				"an explicit final swapchain must replace discovery");
			require(
				DecideAttachment(
					game,
					invalid,
					AttachmentSource::kExplicit,
					AttachmentLifecycle::kActive) ==
					AttachmentDecision::kReject,
				"incomplete render bindings must be rejected");
		});

		runner.test("definitive DXGI failures retire the active attachment", [] {
			require(IsDefinitiveSwapChainLoss(kDxgiErrorDeviceRemoved),
				"device removal must retire the attachment");
			require(IsDefinitiveSwapChainLoss(kDxgiErrorDeviceHung),
				"a device hang must retire the attachment");
			require(IsDefinitiveSwapChainLoss(kDxgiErrorDeviceReset),
				"a device reset must retire the attachment");
			require(IsDefinitiveSwapChainLoss(kDxgiErrorDriverInternal),
				"an internal driver failure must retire the attachment");
			require(!IsDefinitiveSwapChainLoss(0), "success must keep the attachment");
			require(!IsDefinitiveSwapChainLoss(0x887A0001u),
				"a transient invalid call must keep the attachment");
		});

		runner.test("backend reset follows render binding rather than swapchain identity", [] {
			constexpr AttachmentIdentity game{ 1, 2, 3, 4 };
			constexpr AttachmentIdentity proxy{ 5, 2, 3, 4 };
			constexpr AttachmentIdentity newDevice{ 5, 6, 7, 4 };
			constexpr AttachmentIdentity newWindow{ 5, 2, 3, 8 };

			require(
				!RequiresBackendReset(game, proxy, true),
				"a same-binding proxy must reuse the backend");
			require(
				RequiresBackendReset(game, newDevice, true),
				"a device replacement must reset the backend");
			require(
				RequiresBackendReset(game, newWindow, true),
				"a window replacement must reset the backend");
			require(
				!RequiresBackendReset(game, newDevice, false),
				"an uncreated backend needs no reset");
		});

		runner.test("swapchain dispatch survives shadow vtable retargeting", [] {
			require(
				MatchHookDispatch(1, 20, 1, 10) == HookDispatchMatch::kSwapChain,
				"an associated swapchain must keep its captured predecessor after its vptr changes");
			require(
				MatchHookDispatch(2, 10, 1, 10) == HookDispatchMatch::kVtable,
				"an unassociated instance on the patched vtable must use the vtable predecessor");
			require(
				MatchHookDispatch(2, 20, 1, 10) == HookDispatchMatch::kNone,
				"an unrelated instance and vtable must not borrow another predecessor");
			require(
				ReusesHookAssociation(AttachmentLifecycle::kActive, 20, 10),
				"an active proxy must retain its predecessor after shadow-vtable retargeting");
			require(
				ReusesHookAssociation(AttachmentLifecycle::kRetired, 10, 10),
				"a reused address on the same patched vtable must retain its predecessor");
			require(
				!ReusesHookAssociation(AttachmentLifecycle::kRetired, 20, 10),
				"a reused address with a new vtable must establish a new predecessor");
		});

		runner.test("frame telemetry observes only displayed presents", [] {
			require(ObservesDisplayedFrame(0, true), "a successful real Present displays a frame");
			require(!ObservesDisplayedFrame(kPresentTestFlag, true), "DXGI_PRESENT_TEST displays no frame");
			require(!ObservesDisplayedFrame(0, false), "a failed Present displays no frame");
			require(!ObservesDisplayedFrame(kPresentTestFlag, false), "a failed test Present displays no frame");
		});

		runner.test("host initializes eagerly while overlays never suppress game input", [] {
			require(ShouldInitializeHost(true, true),
				"accepted clients did not initialize on an active Present");
			require(!ShouldInitializeHost(false, true),
				"an empty registry initialized the renderer");
			require(ShouldRenderHostFrame(false, true),
				"overlay demand did not produce a frame");
			require(!ShouldSuppressGameInput(false),
				"overlay-only drawing suppressed game input");
			require(ShouldSuppressGameInput(true),
				"a modal menu did not suppress game input");
		});

		runner.test("backbuffer state recreates on identity size and view changes", [] {
			constexpr BackBufferIdentity empty{};
			constexpr BackBufferIdentity first{ 1, 1920, 1080 };
			constexpr BackBufferIdentity replacement{ 2, 1920, 1080 };
			constexpr BackBufferIdentity resized{ 1, 2560, 1440 };
			constexpr BackBufferIdentity invalid{ 1, 0, 1080 };

			require(
				DecideBackBuffer(empty, first, false) == BackBufferDecision::kRecreate,
				"the first valid backbuffer must create an RTV");
			require(
				DecideBackBuffer(first, first, true) == BackBufferDecision::kKeep,
				"an unchanged backbuffer must keep its RTV");
			require(
				DecideBackBuffer(first, first, false) == BackBufferDecision::kRecreate,
				"a missing RTV must be recreated");
			require(
				DecideBackBuffer(first, replacement, true) == BackBufferDecision::kRecreate,
				"a replacement resource must recreate its RTV");
			require(
				DecideBackBuffer(first, resized, true) == BackBufferDecision::kRecreate,
				"a size change must recreate the RTV");
			require(
				DecideBackBuffer(first, invalid, true) == BackBufferDecision::kSkip,
				"an invalid backbuffer must skip rendering");
		});

		runner.test("mouse coordinates map from the client into the backbuffer", [] {
			constexpr MousePosition equal{ 123.75f, 456.25f };
			constexpr auto equalMapped =
				MapClientToBackBuffer(equal, 1920, 1080, 1920, 1080);
			require(equalMapped.x == equal.x && equalMapped.y == equal.y,
				"equal dimensions changed mouse coordinates");

			constexpr auto uniform =
				MapClientToBackBuffer({ 480.0f, 270.0f }, 960, 540, 1920, 1080);
			require(uniform.x == 960.0f && uniform.y == 540.0f,
				"uniform scaling did not match the backbuffer");

			constexpr auto nonUniform =
				MapClientToBackBuffer({ 400.0f, 300.0f }, 800, 600, 2560, 1080);
			require(nonUniform.x == 1280.0f && nonUniform.y == 540.0f,
				"independent axis scaling changed");

			constexpr MousePosition position{ 400.0f, 300.0f };
			constexpr auto zeroClientWidth =
				MapClientToBackBuffer(position, 0, 600, 2560, 1080);
			constexpr auto zeroClientHeight =
				MapClientToBackBuffer(position, 800, 0, 2560, 1080);
			constexpr auto zeroBackBufferWidth =
				MapClientToBackBuffer(position, 800, 600, 0, 1080);
			constexpr auto zeroBackBufferHeight =
				MapClientToBackBuffer(position, 800, 600, 2560, 0);
			require(
				zeroClientWidth.x == position.x && zeroClientWidth.y == position.y &&
					zeroClientHeight.x == position.x && zeroClientHeight.y == position.y &&
					zeroBackBufferWidth.x == position.x && zeroBackBufferWidth.y == position.y &&
					zeroBackBufferHeight.x == position.x && zeroBackBufferHeight.y == position.y,
				"degenerate dimensions changed mouse coordinates");

			constexpr auto unavailable =
				-(std::numeric_limits<float>::max)();
			constexpr auto sentinel =
				MapClientToBackBuffer({ unavailable, unavailable }, 800, 600, 2560, 1080);
			require(sentinel.x == unavailable && sentinel.y == unavailable,
				"the unavailable mouse sentinel was scaled");

			constexpr MousePosition leftOutside{ -1.0f, 300.0f };
			constexpr MousePosition rightOutside{ 800.0f, 300.0f };
			constexpr MousePosition belowOutside{ 400.0f, 601.0f };
			constexpr auto leftMapped =
				MapClientToBackBuffer(leftOutside, 800, 600, 2560, 1080);
			constexpr auto rightMapped =
				MapClientToBackBuffer(rightOutside, 800, 600, 2560, 1080);
			constexpr auto belowMapped =
				MapClientToBackBuffer(belowOutside, 800, 600, 2560, 1080);
			require(
				leftMapped.x == leftOutside.x && leftMapped.y == leftOutside.y &&
					rightMapped.x == rightOutside.x && rightMapped.y == rightOutside.y &&
					belowMapped.x == belowOutside.x && belowMapped.y == belowOutside.y,
				"out-of-window mouse coordinates were scaled into the viewport");
		});

		runner.test("virtual cursor motion integrates and clamps to every display edge", [] {
			constexpr MousePosition position{ 50.0f, 40.0f };
			constexpr auto stable =
				IntegrateVirtualCursor(position, {}, 100, 80);
			require(stable.x == 50.0f && stable.y == 40.0f,
				"zero raw motion changed the virtual cursor");

			constexpr auto negative =
				IntegrateVirtualCursor(position, { -12, -17 }, 100, 80);
			require(negative.x == 38.0f && negative.y == 23.0f,
				"negative raw motion did not integrate one pixel per count");

			constexpr auto left =
				IntegrateVirtualCursor(position, { -1000, 0 }, 100, 80);
			constexpr auto right =
				IntegrateVirtualCursor(position, { 1000, 0 }, 100, 80);
			constexpr auto top =
				IntegrateVirtualCursor(position, { 0, -1000 }, 100, 80);
			constexpr auto bottom =
				IntegrateVirtualCursor(position, { 0, 1000 }, 100, 80);
			require(left.x == 0.0f && left.y == position.y,
				"a large negative delta crossed the left edge");
			require(right.x == 99.0f && right.y == position.y,
				"a large positive delta crossed the right edge");
			require(top.x == position.x && top.y == 0.0f,
				"a large negative delta crossed the top edge");
			require(bottom.x == position.x && bottom.y == 79.0f,
				"a large positive delta crossed the bottom edge");
		});

		runner.test("virtual cursor opening seeds once and resets pending raw motion", [] {
			constexpr auto inBounds =
				SeedVirtualCursor({ 321.0f, 123.0f }, 1920, 1080);
			constexpr auto leftOutside =
				SeedVirtualCursor({ -1.0f, 123.0f }, 1920, 1080);
			constexpr auto rightOutside =
				SeedVirtualCursor({ 1920.0f, 123.0f }, 1920, 1080);
			constexpr auto belowOutside =
				SeedVirtualCursor({ 321.0f, 1080.0f }, 1920, 1080);
			require(inBounds.x == 321.0f && inBounds.y == 123.0f,
				"an in-bounds client cursor did not seed the virtual position");
			require(
				leftOutside.x == 960.0f && leftOutside.y == 540.0f &&
					rightOutside.x == 960.0f && rightOutside.y == 540.0f &&
					belowOutside.x == 960.0f && belowOutside.y == 540.0f,
				"an out-of-bounds client cursor did not seed at display center");

			VirtualCursorState state{};
			state.position = { 10.0f, 20.0f };
			state.accumulated = { 99, -45 };
			state.rawReport = { true, 500, 600 };
			state.active = true;
			state.rawInputObserved = true;
			OpenVirtualCursor(state, { 300.0f, 200.0f }, 1920, 1080);
			require(state.active, "opening did not activate virtual cursor ownership");
			require(state.position.x == 300.0f && state.position.y == 200.0f,
				"opening did not seed from the current client cursor");
			require(state.accumulated.x == 0 && state.accumulated.y == 0,
				"opening retained stale accumulated motion");
			require(!state.rawReport.hasAbsolutePosition && !state.rawInputObserved,
				"opening retained stale raw report state");
		});

		runner.test("raw mouse reports distinguish relative deltas from absolute positions", [] {
			VirtualCursorState relative{};
			OpenVirtualCursor(relative, { 100.0f, 100.0f }, 4000, 3000);
			AccumulateRawMouseReport(relative, 0, 14, -9, 1920, 1080);
			require(relative.rawInputObserved, "a relative raw report was not observed");
			require(relative.accumulated.x == 14 && relative.accumulated.y == -9,
				"a relative raw report was not accumulated directly");
			const auto relativePosition =
				ConsumeVirtualCursorMotion(relative, 4000, 3000);
			require(relativePosition.x == 114.0f && relativePosition.y == 91.0f,
				"relative raw motion did not integrate into the virtual cursor");
			require(relative.accumulated.x == 0 && relative.accumulated.y == 0,
				"consuming raw motion did not reset the accumulator");

			VirtualCursorState absolute{};
			OpenVirtualCursor(absolute, { 100.0f, 100.0f }, 4000, 3000);
			AccumulateRawMouseReport(
				absolute,
				kRawMouseMoveAbsolute | kRawMouseVirtualDesktop,
				32768,
				32768,
				1920,
				1080);
			require(absolute.rawInputObserved, "an absolute raw report was not observed");
			require(absolute.accumulated.x == 0 && absolute.accumulated.y == 0,
				"the first absolute report jumped from an unknown origin");
			AccumulateRawMouseReport(
				absolute,
				kRawMouseMoveAbsolute | kRawMouseVirtualDesktop,
				65535,
				65535,
				1920,
				1080);
			require(absolute.accumulated.x == 960 && absolute.accumulated.y == 540,
				"absolute raw positions were not converted into desktop-pixel deltas");
			const auto absolutePosition =
				ConsumeVirtualCursorMotion(absolute, 4000, 3000);
			require(absolutePosition.x == 1060.0f && absolutePosition.y == 640.0f,
				"absolute raw deltas did not integrate one desktop pixel per count");

			AccumulateRawMouseReport(
				absolute,
				kRawMouseMoveAbsolute,
				0,
				0,
				1920,
				1080);
			const auto negativeAbsolute =
				ConsumeVirtualCursorMotion(absolute, 4000, 3000);
			require(negativeAbsolute.x == 0.0f && negativeAbsolute.y == 0.0f,
				"a decreasing absolute report did not produce a clamped negative delta");
		});

		runner.test("modal cursor falls back only until raw input is observed", [] {
			require(
				DecideModalCursorSource(false) == ModalCursorSource::kAbsoluteFallback,
				"a modal session without raw reports did not retain absolute fallback");
			require(
				DecideModalCursorSource(true) == ModalCursorSource::kRawMotion,
				"an observed raw report did not select virtual cursor motion");
			require(!SendsAbsolutePositionToBackend(kMouseMessageFirst, true),
				"a modal WM_MOUSEMOVE still reached the Win32 backend");
			require(!SendsAbsolutePositionToBackend(kNcMouseMoveMessage, true),
				"a modal WM_NCMOUSEMOVE still reached the Win32 backend");
			require(SendsAbsolutePositionToBackend(kMouseMessageFirst + 1, true),
				"a modal mouse button was suppressed with absolute motion");
			require(SendsAbsolutePositionToBackend(kMouseMessageFirst, false),
				"non-modal mouse movement was suppressed");

			VirtualCursorState state{};
			OpenVirtualCursor(state, { 100.0f, 100.0f }, 1920, 1080);
			const auto fallback =
				FollowAbsoluteCursorFallback(state, { 250.0f, 175.0f }, 1920, 1080);
			require(fallback.x == 250.0f && fallback.y == 175.0f,
				"absolute fallback did not follow the current client cursor");
		});

		runner.test("install state permits one IAT attempt", [] {
			require(AllowsInstallAttempt(InstallState::kNotAttempted), "the first attempt must be allowed");
			require(!AllowsInstallAttempt(InstallState::kRejected), "an unavailable IAT is never retried");
			require(!AllowsInstallAttempt(InstallState::kAttempted), "a started attempt is never repeated");
			require(!AllowsInstallAttempt(InstallState::kInstalled), "installation is idempotent");

			require(IsInstalled(InstallState::kInstalled), "only the installed state is ready");
			require(!IsInstalled(InstallState::kAttempted), "an attempt alone is not ready");
			require(!IsInstalled(InstallState::kRejected), "an unavailable IAT is not ready");
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

		runner.test("window input requires live state under the context lock", [] {
			require(
				HandlesWindowMessage(true, true, true, true),
				"a live backend may handle active-window input");
			require(
				!HandlesWindowMessage(false, true, true, true),
				"an inactive window must only forward input");
			require(
				!HandlesWindowMessage(true, false, true, true),
				"an idle renderer must only forward input");
			require(
				!HandlesWindowMessage(true, true, false, true),
				"an unavailable backend must only forward input");
			require(
				!HandlesWindowMessage(true, true, true, false),
				"a destroyed ImGui context must only forward input");
		});

		runner.test("window hooks retire only after non-client destruction", [] {
			require(RetiresWindowHook(kWindowNcDestroyMessage), "WM_NCDESTROY must retire its hook record");
			require(!RetiresWindowHook(0x0081), "WM_NCCREATE must keep its hook record");
			require(!RetiresWindowHook(0x0002), "WM_DESTROY must preserve the chain through WM_NCDESTROY");
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

			require(
				DecideToggleMessage(kKeyDownMessage, 1, false) ==
					ToggleMessageDecision::kDispatch,
				"a fresh keydown must ask toggle sinks");
			require(
				DecideToggleMessage(kKeyDownMessage, kKeyRepeatBit | 1, true) ==
					ToggleMessageDecision::kConsume,
				"a consumed press must also consume repeats");
			require(
				DecideToggleMessage(kKeyDownMessage, kKeyRepeatBit | 1, false) ==
					ToggleMessageDecision::kForward,
				"an unrelated repeat must reach the game");
			require(
				DecideToggleMessage(kKeyUpMessage, 1, true) ==
					ToggleMessageDecision::kConsumeAndRelease,
				"a consumed press must consume and release its key-up");
			require(
				DecideToggleMessage(kSysKeyUpMessage, 1, true) ==
					ToggleMessageDecision::kConsumeAndRelease,
				"a consumed system press must consume its system key-up");
			require(
				DecideToggleMessage(kKeyUpMessage, 1, false) ==
					ToggleMessageDecision::kForward,
				"an unrelated key-up must reach the game");
		});
	}
}
