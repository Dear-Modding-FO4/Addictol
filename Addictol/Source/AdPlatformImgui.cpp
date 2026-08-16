#include <AdPlatformImgui.h>
#include <AdAnimSubGraphRuntime.h>
#include <AdUtils.h>
#include <RE/B/BSGraphics.h>
#include <RE/C/ControlMap.h>

#include <Windows.h>
#include <d3d11.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <array>
#include <atomic>
#include <utility>

#undef ERROR

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

namespace Addictol
{
	namespace platformImguiDetail
	{
		using namespace ImguiPlatform;

		static_assert(kKeyboardMessageFirst == WM_KEYFIRST && kKeyboardMessageLast == WM_KEYLAST,
			"keyboard message range must match the Windows SDK");
		static_assert(kMouseMessageFirst == WM_MOUSEFIRST && kMouseMessageLast == WM_MOUSELAST,
			"mouse message range must match the Windows SDK");
		static_assert(kKeyDownMessage == WM_KEYDOWN, "key down message must match the Windows SDK");

		using TUIEndFrame = void (*)(void*) noexcept;

		enum class Backend : uint32_t
		{
			kUninitialized,
			kReady,
			kFailed
		};

		static SinkTable<PlatformImguiDrawSink> s_drawSinks{};
		static SinkTable<PlatformImguiToggleSink> s_toggleSinks{};
		static SinkTable<PlatformImguiSetupSink> s_setupSinks{};
		static std::atomic<InstallState> s_installState{ InstallState::kNotAttempted };
		static std::atomic<bool> s_drawingEnabled{ false };
		static std::atomic<Backend> s_backend{ Backend::kUninitialized };
		static std::atomic<TUIEndFrame> s_uiEndFrameOriginal{ nullptr };
		static std::atomic<bool> s_windowReady{ false };

		static HWND s_window{ nullptr };
		static std::atomic<WNDPROC> s_previousWindowProc{ nullptr };
		static bool s_windowIsUnicode{ false };
		static RE::BSGraphics::RendererData* s_rendererData{ nullptr };
		static ID3D11Device* s_expectedDevice{ nullptr };
		static ID3D11Device* s_device{ nullptr };
		static ID3D11DeviceContext* s_immediateContext{ nullptr };
		static std::atomic<bool> s_rendererInvalidLogged{ false };
		static bool s_previousIgnoreKeyboardMouse{ false };
		static bool s_inputSuppressed{ false };

		static INIT_ONCE s_contextLockOnce = INIT_ONCE_STATIC_INIT;
		static CRITICAL_SECTION s_contextLock{};

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
			~ContextLock() noexcept { LeaveCriticalSection(&s_contextLock); }

			ContextLock(const ContextLock&) = delete;
			ContextLock& operator=(const ContextLock&) = delete;
		};

		[[nodiscard]] static Runtime CurrentRuntime() noexcept
		{
			if (RELEX::IsRuntimeOG())
				return Runtime::kOG;
			if (RELEX::IsRuntimeNG())
				return Runtime::kNG;
			return Runtime::kAE;
		}

		static LRESULT CallPreviousWindowProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) noexcept
		{
			const auto previous = s_previousWindowProc.load(std::memory_order_acquire);
			if (!previous)
			{
				return s_windowIsUnicode ?
					DefWindowProcW(a_hwnd, a_msg, a_wparam, a_lparam) :
					DefWindowProcA(a_hwnd, a_msg, a_wparam, a_lparam);
			}
			return s_windowIsUnicode ?
				CallWindowProcW(previous, a_hwnd, a_msg, a_wparam, a_lparam) :
				CallWindowProcA(previous, a_hwnd, a_msg, a_wparam, a_lparam);
		}

		static void ReleaseDevice() noexcept
		{
			if (auto* device = std::exchange(s_device, nullptr))
				device->Release();
		}

		static void ReleaseImmediateContext() noexcept
		{
			if (auto* context = std::exchange(s_immediateContext, nullptr))
				context->Release();
		}

		[[nodiscard]] static bool ValidateDxgiDevice(ID3D11Device* a_device) noexcept
		{
			IDXGIDevice* dxgiDevice{ nullptr };
			IDXGIAdapter* adapter{ nullptr };
			IDXGIFactory* factory{ nullptr };
			const auto valid =
				SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
				SUCCEEDED(dxgiDevice->GetParent(IID_PPV_ARGS(&adapter))) &&
				SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)));
			if (factory)
				factory->Release();
			if (adapter)
				adapter->Release();
			if (dxgiDevice)
				dxgiDevice->Release();
			return valid;
		}

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

		// Runs once on whichever thread the engine calls UIEndFrame from; a failure is permanent, never retried.
		[[nodiscard]] static bool InitializeBackend() noexcept
		{
			if (!ValidateDxgiDevice(s_device))
			{
				ReleaseDevice();
				REX::ERROR("Platform Imgui: the D3D11 device does not expose a valid DXGI parent chain; drawing stays disabled"sv);
				return false;
			}

			auto* context = ImGui::CreateContext();
			if (!context)
			{
				ReleaseDevice();
				REX::ERROR("Platform Imgui: ImGui::CreateContext() failed; drawing stays disabled"sv);
				return false;
			}

			auto& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			// Setup sinks own any persisted geometry path, so nothing is written until one asks for it.
			io.IniFilename = nullptr;
			// Frames only run while drawing is enabled, so the drawn cursor follows that state.
			io.MouseDrawCursor = true;
			// Process DPI awareness is deliberately left to the DPI Scaling module, which owns that user option.

			// Style, fonts, and ini path must be settled before the backend uploads the font atlas.
			for (size_t index = 0, count = s_setupSinks.Size(); index < count; ++index)
				s_setupSinks.At(index)(s_window);

			// Upscalers can leave a proxy in RendererData::context, so ask the device for the real one.
			ID3D11DeviceContext* immediate{ nullptr };
			s_device->GetImmediateContext(&immediate);
			if (!immediate)
			{
				ImGui::DestroyContext(context);
				ReleaseDevice();
				REX::ERROR("Platform Imgui: the D3D11 device returned no immediate context; drawing stays disabled"sv);
				return false;
			}
			s_immediateContext = immediate;

			if (!ImGui_ImplWin32_Init(s_window))
			{
				ReleaseImmediateContext();
				ImGui::DestroyContext(context);
				ReleaseDevice();
				REX::ERROR("Platform Imgui: ImGui_ImplWin32_Init() failed; drawing stays disabled"sv);
				return false;
			}

			if (!ImGui_ImplDX11_Init(s_device, immediate))
			{
				ImGui_ImplWin32_Shutdown();
				ReleaseImmediateContext();
				ImGui::DestroyContext(context);
				ReleaseDevice();
				REX::ERROR("Platform Imgui: ImGui_ImplDX11_Init() failed; drawing stays disabled"sv);
				return false;
			}

			if (!ImGui_ImplDX11_CreateDeviceObjects())
			{
				ImGui_ImplDX11_Shutdown();
				ImGui_ImplWin32_Shutdown();
				ReleaseImmediateContext();
				ImGui::DestroyContext(context);
				ReleaseDevice();
				REX::ERROR("Platform Imgui: D3D11 device-object creation failed; drawing stays disabled"sv);
				return false;
			}

			// The platform keeps one context reference for explicit back-buffer binding.
			ReleaseDevice();
			REX::INFO("Platform Imgui: ImGui initialized against the renderer window and device"sv);
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

			const auto ready = InitializeBackend();
			s_backend.store(ready ? Backend::kReady : Backend::kFailed, std::memory_order_release);
			if (!ready)
			{
				s_drawingEnabled.store(false, std::memory_order_release);
				SetGameInputSuppressed(false);
			}
			return ready;
		}

		static void HKUIEndFrame(void* a_ui) noexcept
		{
			auto original = s_uiEndFrameOriginal.load(std::memory_order_acquire);
			if (original)
				original(a_ui);
			else
				return;

			if (!s_drawingEnabled.load(std::memory_order_acquire))
				return;

			const ContextLock lock;
			if (!BackendReady())
				return;
			const auto& renderWindow = s_rendererData->renderWindow[0];
			auto* backBuffer = reinterpret_cast<ID3D11RenderTargetView*>(
				renderWindow.swapChainRenderTarget.rtView);
			if (reinterpret_cast<HWND>(renderWindow.hwnd) != s_window ||
				reinterpret_cast<ID3D11Device*>(s_rendererData->device) != s_expectedDevice ||
				!s_immediateContext)
			{
				s_drawingEnabled.store(false, std::memory_order_release);
				SetGameInputSuppressed(false);
				s_backend.store(Backend::kFailed, std::memory_order_release);
				if (!s_rendererInvalidLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Platform Imgui: renderer window, device, or back buffer changed; drawing disabled"sv);
				return;
			}
			if (!backBuffer)
				return;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			for (size_t index = 0, count = s_drawSinks.Size(); index < count; ++index)
				s_drawSinks.At(index)();

			ImGui::Render();
			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousTargets{};
			ID3D11DepthStencilView* previousDepth{ nullptr };
			s_immediateContext->OMGetRenderTargets(
				static_cast<UINT>(previousTargets.size()),
				previousTargets.data(),
				&previousDepth);
			s_immediateContext->OMSetRenderTargets(1, &backBuffer, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			s_immediateContext->OMSetRenderTargets(
				static_cast<UINT>(previousTargets.size()),
				previousTargets.data(),
				previousDepth);
			for (auto* target : previousTargets)
			{
				if (target)
					target->Release();
			}
			if (previousDepth)
				previousDepth->Release();
		}

		static LRESULT CALLBACK HKWindowProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) noexcept
		{
			static thread_local bool reentered{ false };
			if (reentered)
				return CallPreviousWindowProc(a_hwnd, a_msg, a_wparam, a_lparam);

			struct ReentryGuard
			{
				bool& value;
				explicit ReentryGuard(bool& a_value) noexcept : value(a_value) { value = true; }
				~ReentryGuard() { value = false; }
			};

			// Toggle sinks run even while closed, because that is how a menu learns it should open.
			if (DispatchesToggleSinks(a_msg, static_cast<uint64_t>(a_lparam)))
			{
				for (size_t index = 0, count = s_toggleSinks.Size(); index < count; ++index)
					s_toggleSinks.At(index)(static_cast<uint32_t>(a_wparam));
			}

			if (!s_drawingEnabled.load(std::memory_order_acquire) ||
				s_backend.load(std::memory_order_acquire) != Backend::kReady)
				return CallPreviousWindowProc(a_hwnd, a_msg, a_wparam, a_lparam);

			LRESULT handled{ 0 };
			bool swallow{ false };
			{
				const ContextLock lock;
				auto& io = ImGui::GetIO();
				{
					const ReentryGuard reentry{ reentered };
					handled = ImGui_ImplWin32_WndProcHandlerEx(a_hwnd, a_msg, a_wparam, a_lparam, io);
				}
				swallow = SwallowsMessage(ClassifyMessage(a_msg), io.WantCaptureMouse, io.WantCaptureKeyboard);
			}

			return swallow ? handled : CallPreviousWindowProc(a_hwnd, a_msg, a_wparam, a_lparam);
		}

		[[nodiscard]] static bool SubclassWindow() noexcept
		{
			s_windowIsUnicode = IsWindowUnicode(s_window) != FALSE;
			const auto current = s_windowIsUnicode ?
				GetWindowLongPtrW(s_window, GWLP_WNDPROC) :
				GetWindowLongPtrA(s_window, GWLP_WNDPROC);
			s_previousWindowProc.store(reinterpret_cast<WNDPROC>(current), std::memory_order_release);
			SetLastError(0);
			const auto previous = s_windowIsUnicode ?
				SetWindowLongPtrW(s_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HKWindowProc)) :
				SetWindowLongPtrA(s_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HKWindowProc));
			if (!previous && GetLastError() != 0)
			{
				REX::WARN("Platform Imgui: window subclassing failed with error {}; installing nothing."sv, GetLastError());
				s_previousWindowProc.store(nullptr, std::memory_order_release);
				return false;
			}

			s_previousWindowProc.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_release);
			return true;
		}

		[[nodiscard]] static bool ValidateUIEndFrame(uintptr_t a_target, uint64_t a_id, Runtime a_runtime) noexcept
		{
			const auto& signature = UIEndFrameSignature(a_runtime);
			if (!RELEX::Validate(a_target, signature) ||
				!AnimSubGraphRuntime::ValidateUniqueSignature(a_target, signature))
			{
				REX::WARN("Platform Imgui: {} UIEndFrame id {} at {:X} failed exact unique-signature validation; installing nothing."sv,
					Describe(a_runtime), a_id, a_target);
				return false;
			}
			return true;
		}

		static void CloseSinkRegistration() noexcept
		{
			s_drawSinks.Close();
			s_toggleSinks.Close();
			s_setupSinks.Close();
		}

	}

	///////////////////////////////////////////////////////////////////////////////

	bool PlatformImgui::RegisterDrawSink(std::string_view a_name, PlatformImguiDrawSink a_sink) noexcept
	{
		using namespace platformImguiDetail;

		const auto result = s_drawSinks.Add(a_name, a_sink);
		if (result != Registration::kAccepted)
			REX::WARN("Platform Imgui: draw sink \"{}\" rejected, {}."sv, a_name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool PlatformImgui::RegisterToggleSink(std::string_view a_name, PlatformImguiToggleSink a_sink) noexcept
	{
		using namespace platformImguiDetail;

		const auto result = s_toggleSinks.Add(a_name, a_sink);
		if (result != Registration::kAccepted)
			REX::WARN("Platform Imgui: key sink \"{}\" rejected, {}."sv, a_name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool PlatformImgui::RegisterSetupSink(std::string_view a_name, PlatformImguiSetupSink a_sink) noexcept
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

		const auto state = s_installState.load(std::memory_order_acquire);
		if (!AllowsInstallAttempt(state))
			return IsInstalled(state);

		// Nobody draws, so the game keeps its untouched code and window procedure.
		if (s_drawSinks.Empty() && s_toggleSinks.Empty())
		{
			CloseSinkRegistration();
			return true;
		}

		const auto runtime = CurrentRuntime();
		const REL::ID id{ kUIEndFrameId.og, kUIEndFrameId.ng, kUIEndFrameId.ae };
		const auto target = id.address();
		if (!ValidateUIEndFrame(target, id.id(), runtime))
		{
			CloseSinkRegistration();
			s_installState.store(InstallState::kRejected, std::memory_order_release);
			return false;
		}

		// Everything past this point may write, so late registration must fail visibly.
		CloseSinkRegistration();
		s_installState.store(InstallState::kAttempted, std::memory_order_release);

		const auto original = RELEX::DetourClassJump(target, &HKUIEndFrame);
		if (!original)
		{
			s_installState.store(InstallState::kIndeterminate, std::memory_order_release);
			REX::ERROR("Platform Imgui: detouring UIEndFrame at {:X} returned zero after Detours may have NOP-filled its prologue; a crash is likely and nothing is rolled back"sv,
				target);
			return false;
		}

		s_uiEndFrameOriginal.store(reinterpret_cast<TUIEndFrame>(original), std::memory_order_release);
		s_installState.store(InstallState::kInstalled, std::memory_order_release);
		REX::INFO("Platform Imgui: {} UIEndFrame id {} detoured at {:X} with {} draw, {} toggle, and {} setup sinks"sv,
			Describe(runtime), id.id(), target, s_drawSinks.Size(), s_toggleSinks.Size(), s_setupSinks.Size());
		return true;
	}

	bool PlatformImgui::InitializeWindow() noexcept
	{
		using namespace platformImguiDetail;

		if (s_drawSinks.Empty() && s_toggleSinks.Empty())
			return true;
		if (!IsInstalled(s_installState.load(std::memory_order_acquire)))
		{
			REX::ERROR("Platform Imgui: sinks were registered but UIEndFrame was not installed during load"sv);
			return false;
		}
		if (s_windowReady.load(std::memory_order_acquire))
			return true;

		auto* rendererData = reinterpret_cast<RE::BSGraphics::RendererData*>(
			REL::ID{ 235166, 2704527, 2704527 }.address());
		s_rendererData = rendererData;
		s_window = reinterpret_cast<HWND>(rendererData->renderWindow[0].hwnd);
		s_device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		s_expectedDevice = s_device;
		if (!s_window || !s_device)
		{
			REX::ERROR("Platform Imgui: the renderer exposes no window or device"sv);
			s_window = nullptr;
			s_device = nullptr;
			return false;
		}

		if (!SubclassWindow())
		{
			s_window = nullptr;
			s_device = nullptr;
			return false;
		}

		s_device->AddRef();
		s_windowReady.store(true, std::memory_order_release);
		REX::INFO("Platform Imgui: renderer window subclassed; ImGui will initialize on first open"sv);
		return true;
	}

	void PlatformImgui::SetDrawingEnabled(bool a_enabled) noexcept
	{
		using namespace platformImguiDetail;

		const ContextLock lock;
		// Enabling before the hook exists would leave a flag set that no frame ever reads.
		const auto enable = a_enabled &&
			IsInstalled(s_installState.load(std::memory_order_acquire)) &&
			s_windowReady.load(std::memory_order_acquire) &&
			s_backend.load(std::memory_order_acquire) != Backend::kFailed;
		s_drawingEnabled.store(enable, std::memory_order_release);
		SetGameInputSuppressed(enable);
	}

	bool PlatformImgui::IsDrawingEnabled() noexcept
	{
		return platformImguiDetail::s_drawingEnabled.load(std::memory_order_acquire);
	}

	bool PlatformImgui::IsReady() noexcept
	{
		using namespace platformImguiDetail;

		return IsInstalled(s_installState.load(std::memory_order_acquire)) &&
			s_windowReady.load(std::memory_order_acquire);
	}

	ImguiPlatform::InstallState PlatformImgui::GetInstallState() noexcept
	{
		return platformImguiDetail::s_installState.load(std::memory_order_acquire);
	}
}
