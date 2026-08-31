#include <DearModdingUI/API.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<DMUI_ImGuiFingerprint>);
static_assert(std::is_trivially_copyable_v<DMUI_ImGuiFingerprint>);
static_assert(std::is_standard_layout_v<DMUI_ClientDescriptor>);
static_assert(std::is_trivially_copyable_v<DMUI_ClientDescriptor>);
static_assert(std::is_standard_layout_v<DMUI_PageDescriptor>);
static_assert(std::is_trivially_copyable_v<DMUI_PageDescriptor>);
static_assert(std::is_standard_layout_v<DMUI_ActionDescriptor>);
static_assert(std::is_trivially_copyable_v<DMUI_ActionDescriptor>);
static_assert(std::is_standard_layout_v<DMUI_HostAPI>);
static_assert(std::is_trivially_copyable_v<DMUI_HostAPI>);
static_assert(sizeof(DMUI_StatusSeverity) == sizeof(uint32_t));
static_assert(!std::is_nothrow_invocable_v<
	DMUI_HostReadyCallback,
	const DMUI_HostReadyInfo*,
	void*>);
static_assert(std::is_nothrow_invocable_v<
	DMUI_AttachSwapChainFn,
	DMUI_ClientHandle,
	void*>);
static_assert(!std::is_nothrow_invocable_v<
	DMUI_ActionCallback,
	void*>);
static_assert(std::is_nothrow_invocable_v<
	DMUI_RegisterActionFn,
	DMUI_ClientHandle,
	const DMUI_ActionDescriptor*,
	DMUI_ActionHandle*>);
static_assert(std::is_nothrow_invocable_v<
	DMUI_SetStatusFn,
	DMUI_ClientHandle,
	DMUI_StatusSeverity,
	const char*>);
static_assert(DMUI_PAGE_KIND_SETTINGS == 1u);
static_assert(DMUI_PAGE_KIND_OVERLAY == 2u);
static_assert(DMUI_STATUS_SEVERITY_INFO == 0u);
static_assert(DMUI_STATUS_SEVERITY_SUCCESS == 1u);
static_assert(DMUI_STATUS_SEVERITY_WARNING == 2u);
static_assert(DMUI_STATUS_SEVERITY_ERROR == 3u);

#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(DMUI_ImGuiFingerprint) == 216);
static_assert(sizeof(DMUI_HostReadyInfo) == 40);
static_assert(sizeof(DMUI_ClientDescriptor) == 72);
static_assert(sizeof(DMUI_PageDescriptor) == 64);
static_assert(sizeof(DMUI_ActionDescriptor) == 64);
static_assert(sizeof(DMUI_HostStateInfo) == 28);
static_assert(sizeof(DMUI_HostAPI) == 96);
static_assert(offsetof(DMUI_HostAPI, structSize) == 0);
static_assert(offsetof(DMUI_HostAPI, apiVersion) == 4);
static_assert(offsetof(DMUI_HostAPI, imguiFingerprint) == 8);
static_assert(offsetof(DMUI_HostAPI, registerClient) == 16);
static_assert(offsetof(DMUI_HostAPI, registerPage) == 24);
static_assert(offsetof(DMUI_HostAPI, queryState) == 32);
static_assert(offsetof(DMUI_HostAPI, requestFrame) == 40);
static_assert(offsetof(DMUI_HostAPI, releaseFrame) == 48);
static_assert(offsetof(DMUI_HostAPI, isMenuVisible) == 56);
static_assert(offsetof(DMUI_HostAPI, selectPage) == 64);
static_assert(offsetof(DMUI_HostAPI, attachSwapChain) == 72);
static_assert(offsetof(DMUI_HostAPI, registerAction) == 80);
static_assert(offsetof(DMUI_HostAPI, setStatus) == 88);
static_assert(DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE == 80);
static_assert(DMUI_HOST_API_REGISTER_ACTION_SIZE == 88);
static_assert(DMUI_HOST_API_SET_STATUS_SIZE == sizeof(DMUI_HostAPI));
#endif
