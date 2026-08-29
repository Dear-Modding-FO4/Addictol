#include <Platform/AdPlatformImgui.h>
#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/CursorLoader.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/Theme.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Core/AdUtils.h>
#include <RE/C/ControlMap.h>
#include <RE/M/MenuCursor.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

#undef ERROR

#ifndef IMGUI_HAS_DOCK
#error "Addictol requires Dear ImGui docking support"
#endif

static_assert(IMGUI_VERSION_NUM == 19291);
static_assert(std::string_view{ IMGUI_VERSION } == "1.92.9b");

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

namespace Addictol
{
	namespace platformImguiDetail
	{
		using namespace ImguiPlatform;

		static_assert(kKeyboardMessageFirst == WM_KEYFIRST && kKeyboardMessageLast == WM_KEYLAST);
		static_assert(kMouseMessageFirst == WM_MOUSEFIRST && kMouseMessageLast == WM_MOUSELAST);
		static_assert(kKeyDownMessage == WM_KEYDOWN);
		static_assert(kKeyUpMessage == WM_KEYUP);
		static_assert(kSysKeyDownMessage == WM_SYSKEYDOWN);
		static_assert(kSysKeyUpMessage == WM_SYSKEYUP);
		static_assert(kPresentTestFlag == DXGI_PRESENT_TEST);
		static_assert(kWindowNcDestroyMessage == WM_NCDESTROY);
		static_assert(kDxgiErrorDeviceRemoved == static_cast<uint32_t>(DXGI_ERROR_DEVICE_REMOVED));
		static_assert(kDxgiErrorDeviceHung == static_cast<uint32_t>(DXGI_ERROR_DEVICE_HUNG));
		static_assert(kDxgiErrorDeviceReset == static_cast<uint32_t>(DXGI_ERROR_DEVICE_RESET));
		static_assert(kDxgiErrorDriverInternal == static_cast<uint32_t>(DXGI_ERROR_DRIVER_INTERNAL_ERROR));

		using TD3D11Create = HRESULT(WINAPI*)(
			IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
			const D3D_FEATURE_LEVEL*, UINT, UINT,
			const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
			ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
		using TPresent = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		using TResizeBuffers = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		enum class Backend : uint32_t
		{
			kUninitialized,
			kReady,
			kFailed
		};

		struct SwapChainHookRecord
		{
			std::atomic<bool> claimed{ false };
			std::atomic<void**> vtable{ nullptr };
			std::atomic<TPresent> present{ nullptr };
			std::atomic<TResizeBuffers> resizeBuffers{ nullptr };
			std::atomic<bool> presentInstalled{ false };
			std::atomic<bool> resizeBuffersInstalled{ false };
		};

		struct SwapChainDispatchRecord
		{
			std::atomic<bool> claimed{ false };
			std::atomic<IDXGISwapChain*> swapChain{ nullptr };
			std::atomic<SwapChainHookRecord*> hook{ nullptr };
		};

		struct WindowHookRecord
		{
			std::atomic<bool> claimed{ false };
			std::atomic<HWND> window{ nullptr };
			std::atomic<WNDPROC> previous{ nullptr };
			std::atomic<bool> unicode{ false };
		};

		struct Attachment
		{
			IDXGISwapChain* swapChain{ nullptr };
			ID3D11Device* device{ nullptr };
			ID3D11DeviceContext* context{ nullptr };
			IDXGIAdapter3* videoMemoryAdapter{ nullptr };
			HWND window{ nullptr };

			[[nodiscard]] AttachmentIdentity Identity() const noexcept
			{
				return {
					reinterpret_cast<uintptr_t>(swapChain),
					reinterpret_cast<uintptr_t>(device),
					reinterpret_cast<uintptr_t>(context),
					reinterpret_cast<uintptr_t>(window)
				};
			}
		};

		static constexpr size_t kSwapChainHookCapacity = 8;
		static constexpr size_t kSwapChainDispatchCapacity = 16;
		static constexpr size_t kWindowHookCapacity = 4;
		static constexpr size_t kShaderClassInstanceCapacity = 256;

		static SinkTable<PlatformImguiDrawSink> s_drawSinks{};
		static SinkTable<PlatformImguiToggleSink> s_toggleSinks{};
		static SinkTable<PlatformImguiSetupSink> s_setupSinks{};
		static std::array<SwapChainHookRecord, kSwapChainHookCapacity> s_swapChainHooks{};
		static std::array<SwapChainDispatchRecord, kSwapChainDispatchCapacity> s_swapChainDispatches{};
		static std::array<WindowHookRecord, kWindowHookCapacity> s_windowHooks{};
		static std::atomic<InstallState> s_installState{ InstallState::kNotAttempted };
		static std::atomic<TD3D11Create> s_originalCreate{ nullptr };
		static std::atomic<IDXGISwapChain*> s_activeSwapChain{ nullptr };
		static std::atomic<HWND> s_activeWindow{ nullptr };
		static std::atomic<bool> s_drawingEnabled{ false };
		static std::atomic<bool> s_windowReady{ false };
		static std::atomic<bool> s_gameLoaded{ false };
		static std::atomic<Backend> s_backend{ Backend::kUninitialized };
		static std::array<std::atomic<bool>, 256> s_consumedToggleKeys{};
		static std::atomic<bool> s_missingPresentOriginalLogged{ false };
		static std::atomic<bool> s_missingResizeOriginalLogged{ false };
		static std::string s_iniPath;

		static Attachment s_attachment{};
		static AttachmentLifecycle s_attachmentLifecycle{ AttachmentLifecycle::kVacant };
		static ID3D11Texture2D* s_backBuffer{ nullptr };
		static ID3D11RenderTargetView* s_backBufferView{ nullptr };
		static BackBufferIdentity s_backBufferIdentity{};
		static bool s_backBufferFailureLogged{ false };
		static bool s_coordinateSpaceLogged{ false };
		static std::atomic<RE::MenuCursor*> s_registeredMenuCursor{ nullptr };
		static bool s_menuCursorUnavailableLogged{ false };
		static bool s_previousIgnoreKeyboardMouse{ false };
		static bool s_inputSuppressed{ false };

		static INIT_ONCE s_contextLockOnce = INIT_ONCE_STATIC_INIT;
		static CRITICAL_SECTION s_contextLock{};

		static HRESULT WINAPI HKPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags) noexcept;
		static HRESULT WINAPI HKResizeBuffers(
			IDXGISwapChain* a_swapChain,
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept;
		static LRESULT CALLBACK HKWindowProc(HWND a_window, UINT a_message, WPARAM a_wparam, LPARAM a_lparam) noexcept;
		static void CloseSinkRegistration() noexcept;
		static void ShutdownBackend() noexcept;

		static BOOL CALLBACK InitializeContextLock(
			[[maybe_unused]] PINIT_ONCE a_once,
			[[maybe_unused]] PVOID a_parameter,
			[[maybe_unused]] PVOID* a_context) noexcept
		{
			InitializeCriticalSection(&s_contextLock);
			return TRUE;
		}

		struct ContextLock
		{
			ContextLock() noexcept
			{
				InitOnceExecuteOnce(&s_contextLockOnce, InitializeContextLock, nullptr, nullptr);
				EnterCriticalSection(&s_contextLock);
			}

			~ContextLock() noexcept
			{
				LeaveCriticalSection(&s_contextLock);
			}

			ContextLock(const ContextLock&) = delete;
			ContextLock& operator=(const ContextLock&) = delete;
		};

		static void SetGameInputSuppressed(bool a_suppressed) noexcept
		{
			auto* controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap)
				return;
			if (a_suppressed && !s_inputSuppressed)
			{
				s_previousIgnoreKeyboardMouse = controlMap->ignoreKeyboardMouse;
				controlMap->SetIgnoreKeyboardMouse(true);
				s_inputSuppressed = true;
			}
			else if (!a_suppressed && s_inputSuppressed)
			{
				controlMap->SetIgnoreKeyboardMouse(s_previousIgnoreKeyboardMouse);
				s_inputSuppressed = false;
			}
		}

		static void ReleaseAttachment(Attachment& a_attachment) noexcept
		{
			if (a_attachment.videoMemoryAdapter)
				a_attachment.videoMemoryAdapter->Release();
			if (a_attachment.context)
				a_attachment.context->Release();
			if (a_attachment.device)
				a_attachment.device->Release();
			if (a_attachment.swapChain)
				a_attachment.swapChain->Release();
			a_attachment = {};
		}

		static void ReleaseBackBuffer() noexcept
		{
			DearModdingUI::BackgroundBlur::InvalidateBackBuffer();
			if (s_backBufferView)
				s_backBufferView->Release();
			if (s_backBuffer)
				s_backBuffer->Release();
			s_backBufferView = nullptr;
			s_backBuffer = nullptr;
			s_backBufferIdentity = {};
		}

		static void ClearConsumedToggleKeys() noexcept
		{
			for (auto& consumed : s_consumedToggleKeys)
				consumed.store(false, std::memory_order_release);
		}

		struct ClientSize
		{
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		[[nodiscard]] static ClientSize ReadClientSize(HWND a_window) noexcept
		{
			RECT client{};
			if (!GetClientRect(a_window, &client) ||
				client.right <= client.left ||
				client.bottom <= client.top)
				return {};
			return {
				static_cast<uint32_t>(client.right - client.left),
				static_cast<uint32_t>(client.bottom - client.top)
			};
		}

		[[nodiscard]] static MousePosition ReadClientMousePosition(HWND a_window) noexcept
		{
			POINT position{};
			if (!GetCursorPos(&position) ||
				!ScreenToClient(a_window, &position))
			{
				constexpr auto unavailable =
					-(std::numeric_limits<float>::max)();
				return { unavailable, unavailable };
			}
			return {
				static_cast<float>(position.x),
				static_cast<float>(position.y)
			};
		}

		[[nodiscard]] static RE::MenuCursor* UpdateEngineCursorRegistration(
			bool a_modalVisible) noexcept
		{
			auto* registered =
				s_registeredMenuCursor.load(std::memory_order_acquire);
			auto* candidate =
				a_modalVisible && !registered ?
				RE::MenuCursor::GetSingleton() :
				nullptr;
			switch (DecideEngineCursorRegistrationTransition(
				registered != nullptr,
				a_modalVisible,
				candidate != nullptr))
			{
			case EngineCursorRegistrationTransition::kAcquire:
				candidate->RegisterCursor();
				s_registeredMenuCursor.store(candidate, std::memory_order_release);
				registered = candidate;
				break;
			case EngineCursorRegistrationTransition::kRelease:
				registered =
					s_registeredMenuCursor.exchange(nullptr, std::memory_order_acq_rel);
				if (registered)
					registered->UnregisterCursor();
				registered = nullptr;
				break;
			default:
				break;
			}
			if (a_modalVisible &&
				!registered &&
				!std::exchange(s_menuCursorUnavailableLogged, true))
			{
				REX::ERROR("Platform Imgui: the engine menu cursor singleton is unavailable"sv);
			}
			return registered;
		}

		struct MenuCursorSnapshot
		{
			MousePosition position{};
			uint32_t registeredCursorCount{ 0 };
			bool available{ false };
		};

		[[nodiscard]] static MenuCursorSnapshot ReadMenuCursor(
			RE::MenuCursor* a_registered) noexcept
		{
			if (!a_registered)
				return {};
			auto* singleton = RE::MenuCursor::GetSingleton();
			if (!singleton || singleton != a_registered)
				return {};
			return {
				{
					static_cast<float>(singleton->cursorPosX),
					static_cast<float>(singleton->cursorPosY)
				},
				singleton->registeredCursors,
				singleton->registeredCursors != 0
			};
		}

		static void ApplyBackBufferCoordinateSpace(
			const MenuCursorSnapshot& a_menuCursor) noexcept
		{
			auto& io = ImGui::GetIO();
			const auto client = ReadClientSize(s_attachment.window);
			const auto mouse = ResolveMenuCursorPosition(
				a_menuCursor.available,
				a_menuCursor.position,
				ReadClientMousePosition(s_attachment.window),
				client.width,
				client.height,
				s_backBufferIdentity.width,
				s_backBufferIdentity.height);
			io.DisplaySize = {
				static_cast<float>(s_backBufferIdentity.width),
				static_cast<float>(s_backBufferIdentity.height)
			};
			io.AddMousePosEvent(mouse.position.x, mouse.position.y);
			if ((client.width != s_backBufferIdentity.width ||
					client.height != s_backBufferIdentity.height) &&
				!std::exchange(s_coordinateSpaceLogged, true))
			{
				REX::INFO("Platform Imgui: client {}x{} differs from backbuffer {}x{}; input is mapped to the backbuffer"sv,
					client.width,
					client.height,
					s_backBufferIdentity.width,
					s_backBufferIdentity.height);
			}
		}

		[[nodiscard]] static LPARAM MapMouseMoveToBackBuffer(
			HWND a_window,
			LPARAM a_lparam) noexcept
		{
			const auto client = ReadClientSize(a_window);
			const MousePosition position{
				static_cast<float>(static_cast<int16_t>(LOWORD(a_lparam))),
				static_cast<float>(static_cast<int16_t>(HIWORD(a_lparam)))
			};
			const auto mapped = MapClientToBackBuffer(
				position,
				client.width,
				client.height,
				s_backBufferIdentity.width,
				s_backBufferIdentity.height);
			if (mapped.x == position.x && mapped.y == position.y)
				return a_lparam;
			return MAKELPARAM(
				static_cast<int16_t>(mapped.x),
				static_cast<int16_t>(mapped.y));
		}

		static void RetireActiveAttachmentLocked(
			IDXGISwapChain* a_swapChain,
			HWND a_window) noexcept
		{
			if ((a_swapChain && s_attachment.swapChain != a_swapChain) ||
				(a_window && s_attachment.window != a_window))
				return;

			s_drawingEnabled.store(false, std::memory_order_release);
			SetGameInputSuppressed(false);
			ReleaseBackBuffer();
			s_backBufferFailureLogged = false;
			ShutdownBackend();
			s_activeSwapChain.store(nullptr, std::memory_order_release);
			s_activeWindow.store(nullptr, std::memory_order_release);
			s_windowReady.store(false, std::memory_order_release);
			ReleaseAttachment(s_attachment);
			s_attachmentLifecycle = AttachmentLifecycle::kRetired;
			ClearConsumedToggleKeys();
		}

		[[nodiscard]] static IDXGIAdapter3* AcquireVideoMemoryAdapter(ID3D11Device* a_device) noexcept
		{
			IDXGIDevice* dxgiDevice{ nullptr };
			IDXGIAdapter* adapter{ nullptr };
			IDXGIAdapter3* adapter3{ nullptr };
			const auto valid =
				SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
				SUCCEEDED(dxgiDevice->GetParent(IID_PPV_ARGS(&adapter))) &&
				SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
			if (adapter)
				adapter->Release();
			if (dxgiDevice)
				dxgiDevice->Release();
			return valid ? adapter3 : nullptr;
		}

		[[nodiscard]] static bool AcquireAttachment(
			IDXGISwapChain* a_swapChain,
			Attachment& a_attachment) noexcept
		{
			if (!a_swapChain)
				return false;

			a_swapChain->AddRef();
			a_attachment.swapChain = a_swapChain;
			if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&a_attachment.device))) ||
				!a_attachment.device)
			{
				ReleaseAttachment(a_attachment);
				return false;
			}

			a_attachment.device->GetImmediateContext(&a_attachment.context);
			DXGI_SWAP_CHAIN_DESC description{};
			if (!a_attachment.context ||
				FAILED(a_swapChain->GetDesc(&description)) ||
				!description.OutputWindow)
			{
				ReleaseAttachment(a_attachment);
				return false;
			}

			a_attachment.window = description.OutputWindow;
			a_attachment.videoMemoryAdapter = AcquireVideoMemoryAdapter(a_attachment.device);
			return true;
		}

		[[nodiscard]] static SwapChainHookRecord* FindSwapChainHook(void** a_vtable) noexcept
		{
			for (auto& record : s_swapChainHooks)
			{
				if (record.vtable.load(std::memory_order_acquire) == a_vtable)
					return &record;
			}
			return nullptr;
		}

		[[nodiscard]] static SwapChainHookRecord* FindAssociatedSwapChainHook(
			IDXGISwapChain* a_swapChain) noexcept
		{
			const auto swapChain = reinterpret_cast<uintptr_t>(a_swapChain);
			for (auto& dispatch : s_swapChainDispatches)
			{
				const auto associated = dispatch.swapChain.load(std::memory_order_acquire);
				if (MatchHookDispatch(
						swapChain,
						0,
						reinterpret_cast<uintptr_t>(associated),
						0) == HookDispatchMatch::kSwapChain)
					return dispatch.hook.load(std::memory_order_acquire);
			}
			return nullptr;
		}

		[[nodiscard]] static SwapChainHookRecord* FindSwapChainDispatch(
			IDXGISwapChain* a_swapChain) noexcept
		{
			if (auto* associated = FindAssociatedSwapChainHook(a_swapChain))
				return associated;
			auto** vtable = *reinterpret_cast<void***>(a_swapChain);
			return FindSwapChainHook(vtable);
		}

		[[nodiscard]] static bool AssociateSwapChainDispatch(
			IDXGISwapChain* a_swapChain,
			SwapChainHookRecord& a_hook) noexcept
		{
			for (auto& dispatch : s_swapChainDispatches)
			{
				if (dispatch.swapChain.load(std::memory_order_acquire) == a_swapChain)
				{
					dispatch.hook.store(&a_hook, std::memory_order_release);
					return true;
				}
			}

			for (auto& dispatch : s_swapChainDispatches)
			{
				bool expected{ false };
				if (!dispatch.claimed.compare_exchange_strong(
						expected, true, std::memory_order_acq_rel))
					continue;
				dispatch.hook.store(&a_hook, std::memory_order_relaxed);
				dispatch.swapChain.store(a_swapChain, std::memory_order_release);
				return true;
			}

			REX::ERROR("Platform Imgui: swapchain dispatch capacity exhausted"sv);
			return false;
		}

		[[nodiscard]] static TPresent FindPreviousPresent(IDXGISwapChain* a_swapChain) noexcept
		{
			auto* record = FindSwapChainDispatch(a_swapChain);
			const auto previous = record ?
				record->present.load(std::memory_order_acquire) : nullptr;
			return previous != &HKPresent ? previous : nullptr;
		}

		[[nodiscard]] static TResizeBuffers FindPreviousResizeBuffers(IDXGISwapChain* a_swapChain) noexcept
		{
			auto* record = FindSwapChainDispatch(a_swapChain);
			const auto previous = record ?
				record->resizeBuffers.load(std::memory_order_acquire) : nullptr;
			return previous != &HKResizeBuffers ? previous : nullptr;
		}

		[[nodiscard]] static bool PatchPresent(SwapChainHookRecord& a_record, void** a_vtable) noexcept
		{
			if (a_record.presentInstalled.load(std::memory_order_acquire))
				return true;

			const auto current = reinterpret_cast<TPresent>(a_vtable[kPresentSlot]);
			if (!current)
				return false;
			if (current == &HKPresent)
			{
				if (a_record.present.load(std::memory_order_acquire) == &HKPresent)
					return false;
				a_record.presentInstalled.store(true, std::memory_order_release);
				return true;
			}

			a_record.present.store(current, std::memory_order_release);
			const auto previous = RELEX::DetourVTable(
				reinterpret_cast<uintptr_t>(a_vtable),
				reinterpret_cast<uintptr_t>(&HKPresent),
				kPresentSlot);
			if (!previous)
				return false;
			a_record.present.store(reinterpret_cast<TPresent>(previous), std::memory_order_release);
			a_record.presentInstalled.store(true, std::memory_order_release);
			return true;
		}

		[[nodiscard]] static bool PatchResizeBuffers(
			SwapChainHookRecord& a_record,
			void** a_vtable) noexcept
		{
			if (a_record.resizeBuffersInstalled.load(std::memory_order_acquire))
				return true;

			const auto current = reinterpret_cast<TResizeBuffers>(a_vtable[kResizeBuffersSlot]);
			if (!current)
				return false;
			if (current == &HKResizeBuffers)
			{
				if (a_record.resizeBuffers.load(std::memory_order_acquire) == &HKResizeBuffers)
					return false;
				a_record.resizeBuffersInstalled.store(true, std::memory_order_release);
				return true;
			}

			a_record.resizeBuffers.store(current, std::memory_order_release);
			const auto previous = RELEX::DetourVTable(
				reinterpret_cast<uintptr_t>(a_vtable),
				reinterpret_cast<uintptr_t>(&HKResizeBuffers),
				kResizeBuffersSlot);
			if (!previous)
				return false;
			a_record.resizeBuffers.store(
				reinterpret_cast<TResizeBuffers>(previous),
				std::memory_order_release);
			a_record.resizeBuffersInstalled.store(true, std::memory_order_release);
			return true;
		}

		[[nodiscard]] static bool InstallSwapChainHooks(
			IDXGISwapChain* a_swapChain,
			AttachmentLifecycle a_lifecycle) noexcept
		{
			auto** vtable = *reinterpret_cast<void***>(a_swapChain);
			if (auto* associated = FindAssociatedSwapChainHook(a_swapChain))
			{
				auto** capturedVtable = associated->vtable.load(std::memory_order_acquire);
				if (ReusesHookAssociation(
						a_lifecycle,
						reinterpret_cast<uintptr_t>(vtable),
						reinterpret_cast<uintptr_t>(capturedVtable)))
				{
					const auto resizeReady = PatchResizeBuffers(*associated, capturedVtable);
					const auto presentReady = PatchPresent(*associated, capturedVtable);
					return resizeReady && presentReady;
				}
			}

			auto* record = FindSwapChainHook(vtable);
			if (!record)
			{
				for (auto& candidate : s_swapChainHooks)
				{
					bool expected{ false };
					if (!candidate.claimed.compare_exchange_strong(
							expected, true, std::memory_order_acq_rel))
						continue;
					candidate.present.store(
						reinterpret_cast<TPresent>(vtable[kPresentSlot]),
						std::memory_order_release);
					candidate.resizeBuffers.store(
						reinterpret_cast<TResizeBuffers>(vtable[kResizeBuffersSlot]),
						std::memory_order_release);
					candidate.vtable.store(vtable, std::memory_order_release);
					record = &candidate;
					break;
				}
			}

			if (!record)
			{
				REX::ERROR("Platform Imgui: swapchain vtable hook capacity exhausted"sv);
				return false;
			}
			if (!AssociateSwapChainDispatch(a_swapChain, *record))
				return false;

			const auto resizeReady = PatchResizeBuffers(*record, vtable);
			const auto presentReady = PatchPresent(*record, vtable);
			if (!resizeReady || !presentReady)
			{
				REX::ERROR("Platform Imgui: swapchain vtable patch failed (Present {}, ResizeBuffers {})"sv,
					presentReady, resizeReady);
				return false;
			}
			return true;
		}

		[[nodiscard]] static WindowHookRecord* FindWindowHook(HWND a_window) noexcept
		{
			for (auto& record : s_windowHooks)
			{
				if (record.window.load(std::memory_order_acquire) == a_window)
					return &record;
			}
			return nullptr;
		}

		static void RetireWindowHook(WindowHookRecord& a_record, HWND a_window) noexcept
		{
			const ContextLock lock;
			auto expectedWindow = a_window;
			if (!a_record.window.compare_exchange_strong(
					expectedWindow, nullptr, std::memory_order_acq_rel))
				return;

			a_record.previous.store(nullptr, std::memory_order_release);
			a_record.unicode.store(false, std::memory_order_release);
			a_record.claimed.store(false, std::memory_order_release);

			if (s_activeWindow.load(std::memory_order_acquire) != a_window)
				return;
			RetireActiveAttachmentLocked(nullptr, a_window);
		}

		static LRESULT CallPreviousWindowProc(
			HWND a_window,
			UINT a_message,
			WPARAM a_wparam,
			LPARAM a_lparam) noexcept
		{
			auto* record = FindWindowHook(a_window);
			const auto unicode = record ?
				record->unicode.load(std::memory_order_acquire) :
				IsWindowUnicode(a_window) != FALSE;
			const auto previous = record ?
				record->previous.load(std::memory_order_acquire) : nullptr;

			const auto result = previous ?
				(unicode ?
						CallWindowProcW(previous, a_window, a_message, a_wparam, a_lparam) :
						CallWindowProcA(previous, a_window, a_message, a_wparam, a_lparam)) :
				(unicode ?
						DefWindowProcW(a_window, a_message, a_wparam, a_lparam) :
						DefWindowProcA(a_window, a_message, a_wparam, a_lparam));
			if (record && RetiresWindowHook(a_message))
				RetireWindowHook(*record, a_window);
			return result;
		}

		[[nodiscard]] static bool SubclassWindow(HWND a_window) noexcept
		{
			if (FindWindowHook(a_window))
			{
				s_windowReady.store(true, std::memory_order_release);
				return true;
			}

			WindowHookRecord* record{ nullptr };
			for (auto& candidate : s_windowHooks)
			{
				bool expected{ false };
				if (candidate.claimed.compare_exchange_strong(
						expected, true, std::memory_order_acq_rel))
				{
					record = &candidate;
					break;
				}
			}
			if (!record)
			{
				REX::ERROR("Platform Imgui: window hook capacity exhausted"sv);
				return false;
			}

			const auto unicode = IsWindowUnicode(a_window) != FALSE;
			const auto current = unicode ?
				GetWindowLongPtrW(a_window, GWLP_WNDPROC) :
				GetWindowLongPtrA(a_window, GWLP_WNDPROC);
			if (reinterpret_cast<WNDPROC>(current) == &HKWindowProc)
			{
				record->claimed.store(false, std::memory_order_release);
				REX::ERROR("Platform Imgui: window hook predecessor is unavailable"sv);
				return false;
			}

			record->unicode.store(unicode, std::memory_order_relaxed);
			record->previous.store(reinterpret_cast<WNDPROC>(current), std::memory_order_release);
			record->window.store(a_window, std::memory_order_release);

			SetLastError(0);
			const auto previous = unicode ?
				SetWindowLongPtrW(a_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HKWindowProc)) :
				SetWindowLongPtrA(a_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HKWindowProc));
			if (!previous && GetLastError() != 0)
			{
				const auto error = GetLastError();
				record->window.store(nullptr, std::memory_order_release);
				record->previous.store(nullptr, std::memory_order_release);
				record->unicode.store(false, std::memory_order_release);
				record->claimed.store(false, std::memory_order_release);
				REX::WARN("Platform Imgui: window subclassing failed with error {}"sv, error);
				return false;
			}

			record->previous.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_release);
			s_windowReady.store(true, std::memory_order_release);
			return true;
		}

		static void ConfigureIniPath(ImGuiIO& a_io) noexcept
		{
			std::error_code error;
			const std::filesystem::path directory{
				AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\DearModdingUI"
			};
			std::filesystem::create_directories(directory, error);
			if (error)
			{
				REX::WARN("DearModdingUI: \"{}\" could not be created; window geometry is not persisted."sv,
					directory.string());
				return;
			}

			s_iniPath = (directory / "imgui.ini").string();
			a_io.IniFilename = s_iniPath.c_str();
			a_io.IniSavingRate = 10.0f;
		}

		static void ShutdownBackend() noexcept
		{
			(void)UpdateEngineCursorRegistration(false);
			if (s_backend.load(std::memory_order_acquire) == Backend::kReady)
			{
				DearModdingUI::CursorLoader::Shutdown();
				DearModdingUI::BackgroundBlur::ResetDeviceResources();
				ImGui_ImplDX11_Shutdown();
				ImGui_ImplWin32_Shutdown();
			}
			s_backend.store(Backend::kUninitialized, std::memory_order_release);
		}

		[[nodiscard]] static bool InitializeBackend() noexcept
		{
			if (!s_attachment.device ||
				!s_attachment.context ||
				!s_attachment.window ||
				!s_windowReady.load(std::memory_order_acquire))
			{
				REX::ERROR("Platform Imgui: the active swapchain has no complete render binding"sv);
				return false;
			}

			auto* context = ImGui::GetCurrentContext();
			const auto createdContext = context == nullptr;
			if (createdContext)
			{
				context = ImGui::CreateContext();
				if (!context)
				{
					REX::ERROR("Platform Imgui: ImGui::CreateContext() failed"sv);
					return false;
				}

				auto& io = ImGui::GetIO();
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
				io.IniFilename = nullptr;
				io.MouseDrawCursor = false;
				ConfigureIniPath(io);

				for (size_t index = 0, count = s_setupSinks.Size(); index < count; ++index)
					s_setupSinks.At(index)(s_attachment.window);
			}

			if (!ImGui_ImplWin32_Init(s_attachment.window))
			{
				if (createdContext)
					ImGui::DestroyContext(context);
				REX::ERROR("Platform Imgui: ImGui_ImplWin32_Init() failed"sv);
				return false;
			}

			if (!ImGui_ImplDX11_Init(s_attachment.device, s_attachment.context))
			{
				ImGui_ImplWin32_Shutdown();
				if (createdContext)
					ImGui::DestroyContext(context);
				REX::ERROR("Platform Imgui: ImGui_ImplDX11_Init() failed"sv);
				return false;
			}

			if (!ImGui_ImplDX11_CreateDeviceObjects())
			{
				ImGui_ImplDX11_Shutdown();
				ImGui_ImplWin32_Shutdown();
				if (createdContext)
					ImGui::DestroyContext(context);
				REX::ERROR("Platform Imgui: D3D11 device-object creation failed"sv);
				return false;
			}

			REX::INFO("Platform Imgui: ImGui initialized on the active swapchain"sv);
			return true;
		}

		[[nodiscard]] static bool BackendReady() noexcept
		{
			switch (s_backend.load(std::memory_order_acquire))
			{
			case Backend::kReady:
				return true;
			case Backend::kFailed:
				return false;
			default:
				break;
			}

			const auto firstInitialization = ImGui::GetCurrentContext() == nullptr;
			if (firstInitialization)
			{
				if (!DearModdingUI::BeginBackendInitialization())
					return false;
				CloseSinkRegistration();
			}
			const auto ready = InitializeBackend();
			s_backend.store(ready ? Backend::kReady : Backend::kFailed, std::memory_order_release);
			if (!ready)
			{
				s_drawingEnabled.store(false, std::memory_order_release);
				SetGameInputSuppressed(false);
				if (firstInitialization)
					DearModdingUI::FailBackendInitialization();
				else
					DearModdingUI::SetBackendUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
			}
			else if (firstInitialization)
				DearModdingUI::CompleteBackendInitialization(ImGui::GetCurrentContext());
			return ready;
		}

		[[nodiscard]] static bool EnsureBackBuffer(IDXGISwapChain* a_swapChain) noexcept
		{
			ID3D11Texture2D* candidate{ nullptr };
			if (FAILED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(&candidate))) || !candidate)
				return false;

			D3D11_TEXTURE2D_DESC description{};
			candidate->GetDesc(&description);
			const BackBufferIdentity identity{
				reinterpret_cast<uintptr_t>(candidate),
				description.Width,
				description.Height
			};
			const auto decision = DecideBackBuffer(
				s_backBufferIdentity,
				identity,
				s_backBufferView != nullptr);
			if (decision == BackBufferDecision::kSkip)
			{
				candidate->Release();
				return false;
			}
			if (decision == BackBufferDecision::kKeep)
			{
				candidate->Release();
				return true;
			}

			ID3D11RenderTargetView* view{ nullptr };
			if (FAILED(s_attachment.device->CreateRenderTargetView(candidate, nullptr, &view)) || !view)
			{
				candidate->Release();
				ReleaseBackBuffer();
				if (!std::exchange(s_backBufferFailureLogged, true))
					REX::ERROR("Platform Imgui: creating the active swapchain backbuffer view failed"sv);
				return false;
			}

			ReleaseBackBuffer();
			s_backBuffer = candidate;
			s_backBufferView = view;
			s_backBufferIdentity = identity;
			s_backBufferFailureLogged = false;
			return true;
		}

		template <class Shader>
		struct ShaderStageState
		{
			Shader* shader{ nullptr };
			std::array<ID3D11ClassInstance*, kShaderClassInstanceCapacity> instances{};
			UINT count{ static_cast<UINT>(instances.size()) };

			void Release() noexcept
			{
				if (shader)
					shader->Release();
				for (UINT index = 0; index < count; ++index)
				{
					if (instances[index])
						instances[index]->Release();
				}
			}
		};

		struct PipelineState
		{
			explicit PipelineState(ID3D11DeviceContext* a_context) noexcept :
				context(a_context)
			{
				context->OMGetRenderTargets(
					static_cast<UINT>(renderTargets.size()),
					renderTargets.data(),
					&depthStencil);
				context->HSGetShader(&hull.shader, hull.instances.data(), &hull.count);
				context->DSGetShader(&domain.shader, domain.instances.data(), &domain.count);
				context->CSGetShader(&compute.shader, compute.instances.data(), &compute.count);
			}

			~PipelineState() noexcept
			{
				context->OMSetRenderTargets(
					static_cast<UINT>(renderTargets.size()),
					renderTargets.data(),
					depthStencil);
				context->HSSetShader(hull.shader, hull.instances.data(), hull.count);
				context->DSSetShader(domain.shader, domain.instances.data(), domain.count);
				context->CSSetShader(compute.shader, compute.instances.data(), compute.count);
				for (auto* target : renderTargets)
				{
					if (target)
						target->Release();
				}
				if (depthStencil)
					depthStencil->Release();
				hull.Release();
				domain.Release();
				compute.Release();
			}

			PipelineState(const PipelineState&) = delete;
			PipelineState& operator=(const PipelineState&) = delete;

			ID3D11DeviceContext* context;
			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ID3D11DepthStencilView* depthStencil{ nullptr };
			ShaderStageState<ID3D11HullShader> hull{};
			ShaderStageState<ID3D11DomainShader> domain{};
			ShaderStageState<ID3D11ComputeShader> compute{};
		};

		static void DrawFrame(IDXGISwapChain* a_swapChain) noexcept
		{
			if (!ShouldInitializeHost(
					DearModdingUI::HasClients(),
					s_windowReady.load(std::memory_order_acquire)) ||
				!BackendReady())
			{
				(void)UpdateEngineCursorRegistration(false);
				return;
			}

			const auto modalVisible = DearModdingUI::IsMenuVisible();
			const auto overlayDemanded = DearModdingUI::NeedsFrame() && !modalVisible;
			s_drawingEnabled.store(modalVisible, std::memory_order_release);
			// Present acquires and samples; serialized teardown may release from another lifecycle thread.
			auto* registeredMenuCursor =
				UpdateEngineCursorRegistration(modalVisible);
			const auto menuCursor = ReadMenuCursor(registeredMenuCursor);
			SetGameInputSuppressed(ShouldSuppressGameInput(modalVisible));
			DearModdingUI::CursorLoader::PrepareFrame(
				modalVisible,
				EngineDrawsMenuCursor(
					registeredMenuCursor != nullptr,
					menuCursor.registeredCursorCount));
			if (!ShouldRenderHostFrame(modalVisible, overlayDemanded) ||
				!EnsureBackBuffer(a_swapChain))
				return;
			if (!DearModdingUI::Theme::PrepareFrame(s_backBufferIdentity.height))
				return;
			DearModdingUI::BackgroundBlur::BeginFrame();

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ApplyBackBufferCoordinateSpace(menuCursor);
			ImGui::NewFrame();
			for (size_t index = 0, count = s_drawSinks.Size(); index < count; ++index)
				s_drawSinks.At(index)();
			ImGui::Render();

			const PipelineState previousState{ s_attachment.context };
			if (modalVisible)
			{
				DearModdingUI::BackgroundBlur::Render(
					s_attachment.device,
					s_attachment.context,
					s_backBuffer,
					s_backBufferView);
			}
			s_attachment.context->OMSetRenderTargets(1, &s_backBufferView, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		static HRESULT WINAPI HKPresent(
			IDXGISwapChain* a_swapChain,
			UINT a_syncInterval,
			UINT a_flags) noexcept
		{
			const auto original = FindPreviousPresent(a_swapChain);
			if (!original)
			{
				if (!s_missingPresentOriginalLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Platform Imgui: Present hook has no previous target"sv);
				return DXGI_ERROR_INVALID_CALL;
			}

			const auto active =
				a_swapChain == s_activeSwapChain.load(std::memory_order_acquire);
			if (active)
			{
				const ContextLock lock;
				if (a_swapChain == s_attachment.swapChain)
				{
					const auto activeWindow = s_activeWindow.load(std::memory_order_acquire);
					if (activeWindow &&
						activeWindow == s_attachment.window &&
						s_gameLoaded.load(std::memory_order_acquire) &&
						!s_windowReady.load(std::memory_order_acquire))
					{
						if (!SubclassWindow(s_attachment.window))
						{
							s_drawingEnabled.store(false, std::memory_order_release);
							SetGameInputSuppressed(false);
							(void)UpdateEngineCursorRegistration(false);
							DearModdingUI::FailBackendInitialization();
						}
					}
					if ((a_flags & DXGI_PRESENT_TEST) == 0)
						DrawFrame(a_swapChain);
				}
			}

			const auto result = original(a_swapChain, a_syncInterval, a_flags);
			if (active && ObservesDisplayedFrame(a_flags, result == S_OK))
				Telemetry::ObserveFrame();
			if (active && IsDefinitiveSwapChainLoss(static_cast<uint32_t>(result)))
			{
				const ContextLock lock;
				RetireActiveAttachmentLocked(a_swapChain, nullptr);
			}
			return result;
		}

		static HRESULT WINAPI HKResizeBuffers(
			IDXGISwapChain* a_swapChain,
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept
		{
			const auto original = FindPreviousResizeBuffers(a_swapChain);
			if (a_swapChain == s_activeSwapChain.load(std::memory_order_acquire))
			{
				const ContextLock lock;
				if (a_swapChain == s_attachment.swapChain)
				{
					ReleaseBackBuffer();
					s_backBufferFailureLogged = false;
				}
			}

			if (original)
			{
				const auto result = original(
					a_swapChain,
					a_bufferCount,
					a_width,
					a_height,
					a_format,
					a_flags);
				if (IsDefinitiveSwapChainLoss(static_cast<uint32_t>(result)))
				{
					const ContextLock lock;
					RetireActiveAttachmentLocked(a_swapChain, nullptr);
				}
				return result;
			}
			if (!s_missingResizeOriginalLogged.exchange(true, std::memory_order_acq_rel))
				REX::ERROR("Platform Imgui: ResizeBuffers hook has no previous target"sv);
			return DXGI_ERROR_INVALID_CALL;
		}

		static LRESULT CALLBACK HKWindowProc(
			HWND a_window,
			UINT a_message,
			WPARAM a_wparam,
			LPARAM a_lparam) noexcept
		{
			static thread_local bool reentered{ false };
			if (reentered ||
				a_window != s_activeWindow.load(std::memory_order_acquire))
				return CallPreviousWindowProc(a_window, a_message, a_wparam, a_lparam);

			struct ReentryGuard
			{
				explicit ReentryGuard(bool& a_value) noexcept :
					value(a_value)
				{
					value = true;
				}

				~ReentryGuard() noexcept
				{
					value = false;
				}

				bool& value;
			};

			const auto keyIndex = static_cast<size_t>(a_wparam);
			const auto trackableKey = keyIndex < s_consumedToggleKeys.size();
			const auto pressConsumed = trackableKey &&
				s_consumedToggleKeys[keyIndex].load(std::memory_order_acquire);
			const auto toggleDecision = DecideToggleMessage(
				a_message,
				static_cast<uint64_t>(a_lparam),
				pressConsumed);
			if (toggleDecision == ToggleMessageDecision::kConsume)
				return 0;
			if (toggleDecision == ToggleMessageDecision::kConsumeAndRelease)
			{
				s_consumedToggleKeys[keyIndex].store(false, std::memory_order_release);
				return 0;
			}
			if (toggleDecision == ToggleMessageDecision::kDispatch)
			{
				bool consumed{ false };
				for (size_t index = 0, count = s_toggleSinks.Size(); index < count; ++index)
				{
					if (s_toggleSinks.At(index)(static_cast<uint32_t>(a_wparam)))
						consumed = true;
				}
				if (consumed)
				{
					if (trackableKey)
						s_consumedToggleKeys[keyIndex].store(true, std::memory_order_release);
					return 0;
				}
			}

			if (DearModdingUI::CursorLoader::HandleWindowMessage(
					a_message, static_cast<uint64_t>(a_lparam)))
				return 1;

			if (!s_drawingEnabled.load(std::memory_order_acquire) ||
				s_backend.load(std::memory_order_acquire) != Backend::kReady)
				return CallPreviousWindowProc(a_window, a_message, a_wparam, a_lparam);

			LRESULT handled{ 0 };
			bool backendHandled{ false };
			bool swallow{ false };
			{
				const ContextLock lock;
				if (HandlesWindowMessage(
						a_window == s_activeWindow.load(std::memory_order_acquire),
						s_drawingEnabled.load(std::memory_order_acquire),
						s_backend.load(std::memory_order_acquire) == Backend::kReady,
						ImGui::GetCurrentContext() != nullptr))
				{
					auto& io = ImGui::GetIO();
					{
						const ReentryGuard reentry{ reentered };
						const auto backendLparam =
							a_message == WM_MOUSEMOVE ?
							MapMouseMoveToBackBuffer(a_window, a_lparam) :
							a_lparam;
						handled = ImGui_ImplWin32_WndProcHandlerEx(
							a_window, a_message, a_wparam, backendLparam, io);
					}
					swallow = SwallowsMessage(
						ClassifyMessage(a_message),
						io.WantCaptureMouse,
						io.WantCaptureKeyboard);
					backendHandled = true;
				}
			}
			return backendHandled && swallow ?
				handled :
				CallPreviousWindowProc(a_window, a_message, a_wparam, a_lparam);
		}

		[[nodiscard]] static bool AttachSwapChain(
			IDXGISwapChain* a_swapChain,
			AttachmentSource a_source) noexcept
		{
			Attachment candidate{};
			if (!AcquireAttachment(a_swapChain, candidate))
			{
				REX::WARN("Platform Imgui: swapchain attachment has no valid D3D11 render binding"sv);
				return false;
			}

			const ContextLock lock;
			const auto currentIdentity = s_attachment.Identity();
			const auto candidateIdentity = candidate.Identity();
			const auto decision = DecideAttachment(
				currentIdentity,
				candidateIdentity,
				a_source,
				s_attachmentLifecycle);
			if (decision == AttachmentDecision::kReject)
			{
				ReleaseAttachment(candidate);
				return false;
			}
			if (decision == AttachmentDecision::kKeepCurrent)
			{
				const auto sameSwapChain =
					currentIdentity.swapChain == candidateIdentity.swapChain;
				ReleaseAttachment(candidate);
				return sameSwapChain || a_source == AttachmentSource::kDiscovery;
			}
			if (!InstallSwapChainHooks(candidate.swapChain, s_attachmentLifecycle))
			{
				ReleaseAttachment(candidate);
				return false;
			}

			const auto resetBackend = RequiresBackendReset(
				currentIdentity,
				candidateIdentity,
				s_backend.load(std::memory_order_acquire) != Backend::kUninitialized);
			const auto windowAlreadyHooked = FindWindowHook(candidate.window) != nullptr;
			(void)UpdateEngineCursorRegistration(false);
			ReleaseBackBuffer();
			s_backBufferFailureLogged = false;
			if (resetBackend)
				ShutdownBackend();
			auto previous = s_attachment;
			s_attachment = candidate;
			candidate = {};
			s_attachmentLifecycle = AttachmentLifecycle::kActive;
			s_activeWindow.store(s_attachment.window, std::memory_order_release);
			s_windowReady.store(windowAlreadyHooked, std::memory_order_release);
			s_activeSwapChain.store(s_attachment.swapChain, std::memory_order_release);
			ReleaseAttachment(previous);

			REX::INFO("Platform Imgui: {} swapchain attached at {}"sv,
				a_source == AttachmentSource::kExplicit ? "explicit" : "discovered",
				static_cast<void*>(s_attachment.swapChain));
			return true;
		}

		static HRESULT WINAPI HKD3D11Create(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_featureLevels,
			UINT a_featureLevelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_description,
			IDXGISwapChain** a_outSwapChain,
			ID3D11Device** a_outDevice,
			D3D_FEATURE_LEVEL* a_outFeatureLevel,
			ID3D11DeviceContext** a_outContext) noexcept
		{
			auto original = s_originalCreate.load(std::memory_order_acquire);
			while (!original &&
				s_installState.load(std::memory_order_acquire) == InstallState::kAttempted)
			{
				SwitchToThread();
				original = s_originalCreate.load(std::memory_order_acquire);
			}
			if (!original)
				return E_FAIL;

			const auto result = original(
				a_adapter,
				a_driverType,
				a_software,
				a_flags,
				a_featureLevels,
				a_featureLevelCount,
				a_sdkVersion,
				a_description,
				a_outSwapChain,
				a_outDevice,
				a_outFeatureLevel,
				a_outContext);
			if (SUCCEEDED(result) && a_outSwapChain && *a_outSwapChain)
			{
				if (!AttachSwapChain(*a_outSwapChain, AttachmentSource::kDiscovery))
					REX::WARN("Platform Imgui: discovered swapchain attachment failed"sv);
			}
			return result;
		}

		static void CloseSinkRegistration() noexcept
		{
			s_drawSinks.Close();
			s_toggleSinks.Close();
			s_setupSinks.Close();
		}
	}

	bool PlatformImgui::RegisterDrawSink(
		std::string_view a_name,
		PlatformImguiDrawSink a_sink) noexcept
	{
		using namespace platformImguiDetail;
		const auto result = s_drawSinks.Add(a_name, a_sink);
		if (result != Registration::kAccepted)
			REX::WARN("Platform Imgui: draw sink \"{}\" rejected, {}."sv, a_name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool PlatformImgui::RegisterToggleSink(
		std::string_view a_name,
		PlatformImguiToggleSink a_sink) noexcept
	{
		using namespace platformImguiDetail;
		const auto result = s_toggleSinks.Add(a_name, a_sink);
		if (result != Registration::kAccepted)
			REX::WARN("Platform Imgui: key sink \"{}\" rejected, {}."sv, a_name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool PlatformImgui::RegisterSetupSink(
		std::string_view a_name,
		PlatformImguiSetupSink a_sink) noexcept
	{
		using namespace platformImguiDetail;
		const auto result = s_setupSinks.Add(a_name, a_sink);
		if (result != Registration::kAccepted)
			REX::WARN("Platform Imgui: setup sink \"{}\" rejected, {}."sv, a_name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool PlatformImgui::InstallHooks() noexcept
	{
		using namespace platformImguiDetail;
		auto expected = InstallState::kNotAttempted;
		if (!s_installState.compare_exchange_strong(
				expected,
				InstallState::kAttempted,
				std::memory_order_acq_rel))
			return IsInstalled(expected);

		const auto original = reinterpret_cast<TD3D11Create>(RELEX::DetourIAT(
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&HKD3D11Create)));
		if (!original)
		{
			CloseSinkRegistration();
			s_installState.store(InstallState::kRejected, std::memory_order_release);
			REX::ERROR("Platform Imgui: D3D11CreateDeviceAndSwapChain was not found in the IAT"sv);
			DearModdingUI::SetBackendUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
			return false;
		}

		s_originalCreate.store(original, std::memory_order_release);
		s_installState.store(InstallState::kInstalled, std::memory_order_release);
		REX::INFO("Platform Imgui: D3D11 swapchain discovery installed with {} draw, {} toggle, and {} setup sinks"sv,
			s_drawSinks.Size(), s_toggleSinks.Size(), s_setupSinks.Size());
		return true;
	}

	bool PlatformImgui::InitializeWindow() noexcept
	{
		using namespace platformImguiDetail;
		s_gameLoaded.store(true, std::memory_order_release);
		if (s_drawSinks.Empty() && s_toggleSinks.Empty())
			return true;
		if (!IsInstalled(s_installState.load(std::memory_order_acquire)))
		{
			REX::ERROR("Platform Imgui: sinks were registered but swapchain discovery was not installed"sv);
			return false;
		}

		const ContextLock lock;
		if (!s_attachment.swapChain)
		{
			REX::INFO("Platform Imgui: waiting for a discovered or explicit final swapchain"sv);
			return true;
		}
		if (!SubclassWindow(s_attachment.window))
			return false;

		REX::INFO("Platform Imgui: active swapchain window subclassed; ImGui will initialize on Present"sv);
		return true;
	}

	bool PlatformImgui::AttachSwapChain(IDXGISwapChain* a_swapChain) noexcept
	{
		return platformImguiDetail::AttachSwapChain(
			a_swapChain,
			ImguiPlatform::AttachmentSource::kExplicit);
	}

	void PlatformImgui::SetDrawingEnabled(bool a_enabled) noexcept
	{
		using namespace platformImguiDetail;
		const ContextLock lock;
		const auto enable = a_enabled &&
			IsInstalled(s_installState.load(std::memory_order_acquire)) &&
			s_activeSwapChain.load(std::memory_order_acquire) != nullptr &&
			s_windowReady.load(std::memory_order_acquire) &&
			s_backend.load(std::memory_order_acquire) == Backend::kReady;
		s_drawingEnabled.store(enable, std::memory_order_release);
		SetGameInputSuppressed(enable);
		if (!enable)
			(void)UpdateEngineCursorRegistration(false);
		if (ImGui::GetCurrentContext())
			DearModdingUI::CursorLoader::PrepareFrame(enable, false);
	}

	bool PlatformImgui::IsDrawingEnabled() noexcept
	{
		return platformImguiDetail::s_drawingEnabled.load(std::memory_order_acquire);
	}

	bool PlatformImgui::IsReady() noexcept
	{
		using namespace platformImguiDetail;
		return IsInstalled(s_installState.load(std::memory_order_acquire)) &&
			s_activeSwapChain.load(std::memory_order_acquire) != nullptr &&
			s_windowReady.load(std::memory_order_acquire) &&
			s_backend.load(std::memory_order_acquire) == Backend::kReady;
	}

	bool PlatformImgui::QueryVideoMemory(uint64_t& a_used, uint64_t& a_budget) noexcept
	{
		using namespace platformImguiDetail;
		const ContextLock lock;
		if (!s_attachment.videoMemoryAdapter)
			return false;

		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		if (FAILED(s_attachment.videoMemoryAdapter->QueryVideoMemoryInfo(
				0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
			return false;
		a_used = info.CurrentUsage;
		a_budget = info.Budget;
		return true;
	}

	ImguiPlatform::InstallState PlatformImgui::GetInstallState() noexcept
	{
		return platformImguiDetail::s_installState.load(std::memory_order_acquire);
	}

	std::string PlatformImgui::GetConfigurePath() noexcept
	{
		using namespace platformImguiDetail;
		const ContextLock lock;
		return s_iniPath;
	}
}
