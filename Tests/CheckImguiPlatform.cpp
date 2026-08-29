#include "../Addictol/Include/Platform/AdImguiPlatformTargets.h"
#include "Harness.h"

#include <string>

namespace
{
	using namespace Addictol::ImguiPlatform;

	void draw_sink_a() noexcept {}
	void draw_sink_b() noexcept {}
	void key_sink(uint32_t) noexcept {}

	using DrawSink = void (*)() noexcept;
	using KeySink = void (*)(uint32_t) noexcept;
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
				DecideAttachment(empty, game, AttachmentSource::kDiscovery) ==
					AttachmentDecision::kAttach,
				"the first discovered swapchain must attach");
			require(
				DecideAttachment(game, sameGame, AttachmentSource::kExplicit) ==
					AttachmentDecision::kKeepCurrent,
				"reattaching the same swapchain must be idempotent");
			require(
				DecideAttachment(game, reboundGame, AttachmentSource::kDiscovery) ==
					AttachmentDecision::kAttach,
				"a captured swapchain with a changed binding must refresh its attachment");
			require(
				DecideAttachment(game, other, AttachmentSource::kDiscovery) ==
					AttachmentDecision::kKeepCurrent,
				"later discovery must not select an unrelated swapchain");
			require(
				DecideAttachment(game, other, AttachmentSource::kExplicit) ==
					AttachmentDecision::kAttach,
				"an explicit final swapchain must replace discovery");
			require(
				DecideAttachment(game, invalid, AttachmentSource::kExplicit) ==
					AttachmentDecision::kReject,
				"incomplete render bindings must be rejected");
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
				HandlesWindowMessage(true, true, true, false, true),
				"a live backend may handle active-window input");
			require(
				!HandlesWindowMessage(false, true, true, false, true),
				"an inactive window must only forward input");
			require(
				!HandlesWindowMessage(true, false, true, false, true),
				"an idle renderer must only forward input");
			require(
				!HandlesWindowMessage(true, true, false, false, true),
				"an unavailable backend must only forward input");
			require(
				!HandlesWindowMessage(true, true, true, true, true),
				"a pending backend reset must only forward input");
			require(
				!HandlesWindowMessage(true, true, true, false, false),
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
		});
	}
}
