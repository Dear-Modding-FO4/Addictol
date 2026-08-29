#define DMUI_HOST_EXPORTS
#include <DearModdingUI/Host.h>
#include <DearModdingUI/ImGuiRecovery.h>
#include <Platform/AdPlatformImgui.h>

#include <REX/REX.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <DearModdingUI/ImGuiFingerprint.h>

#include <atomic>
#include <limits>
#include <mutex>

#ifndef IMGUI_HAS_DOCK
#error "DearModdingUI requires the pinned Dear ImGui docking build"
#endif

static_assert(IMGUI_VERSION_NUM == DMUI_IMGUI_VERSION_NUM);
static_assert(sizeof(DMUI_IMGUI_UPSTREAM_COMMIT) == 41);

namespace Addictol::DearModdingUI
{
	using namespace std::literals;

	namespace
	{
		struct AllocatorState
		{
			ImGuiMemAllocFunc alloc{ nullptr };
			ImGuiMemFreeFunc free{ nullptr };
			void* userData{ nullptr };
		};

		struct Service
		{
			Service() :
				registry(HostFingerprint())
			{}

			Registry registry;
			std::atomic<DMUI_HostState> state{ DMUI_HOST_STATE_NOT_INITIALIZED };
			std::atomic<DMUI_UnavailableReason> unavailableReason{ DMUI_UNAVAILABLE_NONE };
			std::atomic<DMUI_UnavailableReason> deferredUnavailableReason{ DMUI_UNAVAILABLE_NONE };
			std::atomic<bool> menuVisible{ false };
			std::atomic<DMUI_PageHandle> selectedPage{ DMUI_INVALID_PAGE_HANDLE };
			AllocatorState allocator;
		};

		[[nodiscard]] Service& GetService() noexcept
		{
			static Service service;
			return service;
		}

		[[nodiscard]] DMUI_Result StateResult(DMUI_HostState a_state) noexcept
		{
			switch (a_state)
			{
			case DMUI_HOST_STATE_NOT_INITIALIZED:
				return DMUI_RESULT_HOST_NOT_INITIALIZED;
			case DMUI_HOST_STATE_READY:
				return DMUI_RESULT_OK;
			case DMUI_HOST_STATE_UNAVAILABLE:
				return GetService().unavailableReason.load(std::memory_order_acquire) ==
						DMUI_UNAVAILABLE_HOST_DISABLED ?
					DMUI_RESULT_HOST_DISABLED :
					DMUI_RESULT_BACKEND_FAILED;
			default:
				return DMUI_RESULT_HOST_NOT_READY;
			}
		}

		[[nodiscard]] void* AllocCpp(size_t a_size, void* a_userData) noexcept
		{
			auto* allocator = static_cast<AllocatorState*>(a_userData);
			try
			{
				return allocator && allocator->alloc ?
					allocator->alloc(a_size, allocator->userData) :
					nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		[[nodiscard]] void* DMUI_CALL Alloc(size_t a_size, void* a_userData) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				return AllocCpp(a_size, a_userData);
			}
			__except (1)
			{
				return nullptr;
			}
#else
			return AllocCpp(a_size, a_userData);
#endif
		}

		void FreeCpp(void* a_allocation, void* a_userData) noexcept
		{
			auto* allocator = static_cast<AllocatorState*>(a_userData);
			try
			{
				if (allocator && allocator->free)
					allocator->free(a_allocation, allocator->userData);
			}
			catch (...)
			{}
		}

		void DMUI_CALL Free(void* a_allocation, void* a_userData) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				FreeCpp(a_allocation, a_userData);
			}
			__except (1)
			{}
#else
			FreeCpp(a_allocation, a_userData);
#endif
		}

		[[nodiscard]] DMUI_Result RegisterClient(
			const DMUI_ClientDescriptor* a_descriptor,
			DMUI_ClientHandle* a_client,
			ClientOrigin a_origin) noexcept
		{
			if (!a_descriptor || !a_client)
				return DMUI_RESULT_INVALID_ARGUMENT;
			*a_client = DMUI_INVALID_CLIENT_HANDLE;
			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state == DMUI_HOST_STATE_INITIALIZING ||
				state == DMUI_HOST_STATE_READY)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			if (state != DMUI_HOST_STATE_WAITING_FOR_PRESENT)
				return StateResult(state);
			return service.registry.RegisterClient(a_descriptor, a_client, a_origin);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRegisterClientCpp(
			const DMUI_ClientDescriptor* a_descriptor,
			DMUI_ClientHandle* a_client) noexcept
		{
			return RegisterClient(a_descriptor, a_client, ClientOrigin::kExternal);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRegisterPageCpp(
			DMUI_ClientHandle a_client,
			const DMUI_PageDescriptor* a_descriptor,
			DMUI_PageHandle* a_page) noexcept
		{
			if (!a_descriptor || !a_page || a_client == DMUI_INVALID_CLIENT_HANDLE)
				return DMUI_RESULT_INVALID_ARGUMENT;
			*a_page = DMUI_INVALID_PAGE_HANDLE;
			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state == DMUI_HOST_STATE_INITIALIZING ||
				state == DMUI_HOST_STATE_READY)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			if (state != DMUI_HOST_STATE_WAITING_FOR_PRESENT)
				return StateResult(state);
			return service.registry.RegisterPage(a_client, a_descriptor, a_page);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiQueryStateCpp(
			DMUI_HostStateInfo* a_state) noexcept
		{
			if (!a_state)
				return DMUI_RESULT_INVALID_ARGUMENT;
			if (a_state->structSize < sizeof(DMUI_HostStateInfo))
				return DMUI_RESULT_STRUCT_TOO_SMALL;

			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			const auto clientCount = service.registry.ClientCount();
			const auto pageCount = service.registry.PageCount();
			const auto demandedCount = service.registry.DemandedOverlayCount();
			if (clientCount > (std::numeric_limits<uint32_t>::max)() ||
				pageCount > (std::numeric_limits<uint32_t>::max)() ||
				demandedCount > (std::numeric_limits<uint32_t>::max)())
				return DMUI_RESULT_RESOURCE_EXHAUSTED;

			a_state->state = state;
			a_state->unavailableReason =
				service.unavailableReason.load(std::memory_order_acquire);
			a_state->registrationOpen =
				state == DMUI_HOST_STATE_WAITING_FOR_PRESENT && service.registry.IsOpen() ?
				1u :
				0u;
			a_state->clientCount = static_cast<uint32_t>(clientCount);
			a_state->pageCount = static_cast<uint32_t>(pageCount);
			a_state->demandedOverlayCount = static_cast<uint32_t>(demandedCount);
			return StateResult(state);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRequestFrameCpp(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			if (a_client == DMUI_INVALID_CLIENT_HANDLE ||
				a_page == DMUI_INVALID_PAGE_HANDLE)
				return DMUI_RESULT_INVALID_ARGUMENT;
			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state == DMUI_HOST_STATE_NOT_INITIALIZED ||
				state == DMUI_HOST_STATE_UNAVAILABLE)
				return StateResult(state);
			return service.registry.RequestFrame(a_client, a_page);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiReleaseFrameCpp(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			if (a_client == DMUI_INVALID_CLIENT_HANDLE ||
				a_page == DMUI_INVALID_PAGE_HANDLE)
				return DMUI_RESULT_INVALID_ARGUMENT;
			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state == DMUI_HOST_STATE_NOT_INITIALIZED ||
				state == DMUI_HOST_STATE_UNAVAILABLE)
				return StateResult(state);
			return service.registry.ReleaseFrame(a_client, a_page);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiIsMenuVisibleCpp(uint32_t* a_visible) noexcept
		{
			if (!a_visible)
				return DMUI_RESULT_INVALID_ARGUMENT;
			const auto state = GetService().state.load(std::memory_order_acquire);
			*a_visible = state == DMUI_HOST_STATE_READY && IsMenuVisible() ? 1u : 0u;
			return StateResult(state);
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiSelectPageCpp(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			if (a_client == DMUI_INVALID_CLIENT_HANDLE ||
				a_page == DMUI_INVALID_PAGE_HANDLE)
				return DMUI_RESULT_INVALID_ARGUMENT;
			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state != DMUI_HOST_STATE_READY)
				return StateResult(state);
			const auto valid = service.registry.ValidatePage(
				a_client, a_page, DMUI_PAGE_KIND_SETTINGS);
			if (valid != DMUI_RESULT_OK)
				return valid;
			service.selectedPage.store(a_page, std::memory_order_release);
			service.menuVisible.store(true, std::memory_order_release);
			return DMUI_RESULT_OK;
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiAttachSwapChainCpp(
			DMUI_ClientHandle a_client,
			void* a_nativeSwapChain) noexcept
		{
			if (a_client == DMUI_INVALID_CLIENT_HANDLE || !a_nativeSwapChain)
				return DMUI_RESULT_INVALID_ARGUMENT;

			auto& service = GetService();
			const auto state = service.state.load(std::memory_order_acquire);
			if (state == DMUI_HOST_STATE_INITIALIZING)
				return DMUI_RESULT_RENDERER_BUSY;
			if (state != DMUI_HOST_STATE_WAITING_FOR_PRESENT &&
				state != DMUI_HOST_STATE_READY)
				return StateResult(state);

			const auto clientResult = service.registry.ValidateSwapChainClient(a_client);
			if (clientResult != DMUI_RESULT_OK)
				return clientResult;
			return PlatformImgui::AttachSwapChain(
					   static_cast<IDXGISwapChain*>(a_nativeSwapChain)) ?
				DMUI_RESULT_OK :
				DMUI_RESULT_SWAPCHAIN_REJECTED;
		}

		template <class Function>
		[[nodiscard]] DMUI_Result GuardApiCall(Function&& a_function) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				return a_function();
			}
			__except (1)
			{
				return DMUI_RESULT_INVALID_ARGUMENT;
			}
#else
			return a_function();
#endif
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRegisterClient(
			const DMUI_ClientDescriptor* a_descriptor,
			DMUI_ClientHandle* a_client) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiRegisterClientCpp(a_descriptor, a_client);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRegisterPage(
			DMUI_ClientHandle a_client,
			const DMUI_PageDescriptor* a_descriptor,
			DMUI_PageHandle* a_page) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiRegisterPageCpp(a_client, a_descriptor, a_page);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiQueryState(DMUI_HostStateInfo* a_state) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiQueryStateCpp(a_state);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiRequestFrame(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiRequestFrameCpp(a_client, a_page);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiReleaseFrame(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiReleaseFrameCpp(a_client, a_page);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiIsMenuVisible(uint32_t* a_visible) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiIsMenuVisibleCpp(a_visible);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiSelectPage(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiSelectPageCpp(a_client, a_page);
			});
		}

		[[nodiscard]] DMUI_Result DMUI_CALL ApiAttachSwapChain(
			DMUI_ClientHandle a_client,
			void* a_nativeSwapChain) noexcept
		{
			return GuardApiCall([&]() noexcept {
				return ApiAttachSwapChainCpp(a_client, a_nativeSwapChain);
			});
		}
	}

	const DMUI_ImGuiFingerprint& HostFingerprint() noexcept
	{
		static const DMUI_ImGuiFingerprint fingerprint = DMUI_MakeImGuiFingerprint();
		return fingerprint;
	}

	const DMUI_HostAPI& HostAPI() noexcept
	{
		static const DMUI_HostAPI api{
			sizeof(DMUI_HostAPI),
			DMUI_API_VERSION_CURRENT,
			&HostFingerprint(),
			&ApiRegisterClient,
			&ApiRegisterPage,
			&ApiQueryState,
			&ApiRequestFrame,
			&ApiReleaseFrame,
			&ApiIsMenuVisible,
			&ApiSelectPage,
			&ApiAttachSwapChain
		};
		return api;
	}

	void Initialize() noexcept
	{
		auto& service = GetService();
		auto expected = DMUI_HOST_STATE_NOT_INITIALIZED;
		service.state.compare_exchange_strong(
			expected,
			DMUI_HOST_STATE_WAITING_FOR_PRESENT,
			std::memory_order_acq_rel);
	}

	void SetBackendUnavailable(DMUI_UnavailableReason a_reason) noexcept
	{
		auto& service = GetService();
		const auto state = service.state.load(std::memory_order_acquire);
		if (state == DMUI_HOST_STATE_UNAVAILABLE)
			return;
		service.unavailableReason.store(a_reason, std::memory_order_release);
		service.state.store(DMUI_HOST_STATE_UNAVAILABLE, std::memory_order_release);
		service.menuVisible.store(false, std::memory_order_release);
	}

	void DeferBackendUnavailable(DMUI_UnavailableReason a_reason) noexcept
	{
		GetService().deferredUnavailableReason.store(a_reason, std::memory_order_release);
	}

	bool BeginBackendInitialization() noexcept
	{
		auto& service = GetService();
		if (service.registry.Empty())
			return false;
		auto expected = DMUI_HOST_STATE_WAITING_FOR_PRESENT;
		if (!service.state.compare_exchange_strong(
				expected,
				DMUI_HOST_STATE_INITIALIZING,
				std::memory_order_acq_rel))
			return expected == DMUI_HOST_STATE_INITIALIZING ||
				expected == DMUI_HOST_STATE_READY;
		if (!service.registry.Freeze())
		{
			FailBackendInitialization();
			return false;
		}
		if (service.deferredUnavailableReason.load(std::memory_order_acquire) !=
			DMUI_UNAVAILABLE_NONE)
		{
			FailBackendInitialization();
			return false;
		}
		return true;
	}

	void CompleteBackendInitialization(void* a_imguiContext) noexcept
	{
		auto& service = GetService();
		auto expected = DMUI_HOST_STATE_INITIALIZING;
		if (!service.state.compare_exchange_strong(
				expected,
				DMUI_HOST_STATE_READY,
				std::memory_order_acq_rel))
			return;

		ImGui::GetAllocatorFunctions(
			&service.allocator.alloc,
			&service.allocator.free,
			&service.allocator.userData);
		const DMUI_HostReadyInfo info{
			sizeof(DMUI_HostReadyInfo),
			DMUI_API_VERSION_CURRENT,
			a_imguiContext,
			&Alloc,
			&Free,
			&service.allocator
		};
		service.registry.NotifyReady(info);
	}

	void FailBackendInitialization() noexcept
	{
		auto& service = GetService();
		if (service.state.exchange(
				DMUI_HOST_STATE_UNAVAILABLE,
				std::memory_order_acq_rel) == DMUI_HOST_STATE_UNAVAILABLE)
			return;
		service.unavailableReason.store(
			DMUI_UNAVAILABLE_BACKEND_FAILED,
			std::memory_order_release);
		service.menuVisible.store(false, std::memory_order_release);
		if (service.registry.IsOpen())
			(void)service.registry.Freeze();
		service.registry.NotifyUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
	}

	bool HasClients() noexcept
	{
		return !GetService().registry.Empty();
	}

	bool NeedsFrame() noexcept
	{
		auto& service = GetService();
		return service.menuVisible.load(std::memory_order_acquire) ||
			service.registry.DemandedOverlayCount() != 0;
	}

	bool HasSettingsPages() noexcept
	{
		return GetService().registry.HasSettingsPages();
	}

	bool IsMenuVisible() noexcept
	{
		return GetService().menuVisible.load(std::memory_order_acquire);
	}

	DMUI_Result SetMenuVisible(bool a_visible) noexcept
	{
		auto& service = GetService();
		const auto state = service.state.load(std::memory_order_acquire);
		if (state != DMUI_HOST_STATE_READY)
			return StateResult(state);
		if (a_visible && !service.registry.HasSettingsPages())
			return DMUI_RESULT_PAGE_NOT_FOUND;
		service.menuVisible.store(a_visible, std::memory_order_release);
		return DMUI_RESULT_OK;
	}

	DMUI_PageHandle SelectedPage() noexcept
	{
		return GetService().selectedPage.load(std::memory_order_acquire);
	}

	void ClearPageSelection(DMUI_PageHandle a_page) noexcept
	{
		auto& selected = GetService().selectedPage;
		selected.compare_exchange_strong(
			a_page,
			DMUI_INVALID_PAGE_HANDLE,
			std::memory_order_acq_rel);
	}

	bool DrawPage(DMUI_PageHandle a_page) noexcept
	{
		auto& service = GetService();
		auto recovery = ImGuiRecoverySnapshot::Capture();
		if (!recovery)
		{
			service.registry.MarkPageFailed(a_page);
			REX::ERROR("DearModdingUI: page callback {} could not be isolated and was disabled"sv,
				a_page);
			return false;
		}
		const auto result = service.registry.InvokePage(a_page);
		if (result == DMUI_RESULT_CALLBACK_FAILED)
		{
			recovery->RecoverFailure();
			REX::ERROR("DearModdingUI: page callback {} failed and was disabled"sv, a_page);
			return false;
		}
		recovery->RecoverAfterCallback();
		return result == DMUI_RESULT_OK;
	}

	bool PageFailed(DMUI_PageHandle a_page) noexcept
	{
		return GetService().registry.PageFailed(a_page);
	}

	void DrawDemandedOverlays() noexcept
	{
		auto& registry = GetService().registry;
		for (const auto& page : registry.OrderedPages())
		{
			if (page.kind == DMUI_PAGE_KIND_OVERLAY &&
				registry.IsFrameDemanded(page.handle))
				(void)DrawPage(page.handle);
		}
	}

	const std::vector<RegisteredPage>& OrderedPages() noexcept
	{
		return GetService().registry.OrderedPages();
	}

	const NavigationModel& Navigation() noexcept
	{
		return GetService().registry.Navigation();
	}

	DMUI_Result RegisterInternalClient(
		const DMUI_ClientDescriptor* a_descriptor,
		DMUI_ClientHandle* a_client) noexcept
	{
		return RegisterClient(a_descriptor, a_client, ClientOrigin::kHost);
	}
}

DMUI_EXPORT const DMUI_HostAPI* DMUI_CALL DMUI_GetHostAPI(
	uint32_t a_requestedVersion) noexcept
{
	return Addictol::DearModdingUI::Registry::SupportsVersion(a_requestedVersion) ?
		&Addictol::DearModdingUI::HostAPI() :
		nullptr;
}
