#include "../Addictol/Include/DearModdingUI/API.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<DMUI_ImGuiFingerprint>);
static_assert(std::is_trivially_copyable_v<DMUI_ImGuiFingerprint>);
static_assert(std::is_standard_layout_v<DMUI_ClientDescriptor>);
static_assert(std::is_trivially_copyable_v<DMUI_ClientDescriptor>);
static_assert(std::is_standard_layout_v<DMUI_PageDescriptor>);
static_assert(std::is_trivially_copyable_v<DMUI_PageDescriptor>);
static_assert(std::is_standard_layout_v<DMUI_HostAPI>);
static_assert(std::is_trivially_copyable_v<DMUI_HostAPI>);
static_assert(!std::is_nothrow_invocable_v<
	DMUI_HostReadyCallback,
	const DMUI_HostReadyInfo*,
	void*>);
static_assert(std::is_nothrow_invocable_v<
	DMUI_AttachSwapChainFn,
	DMUI_ClientHandle,
	void*>);
static_assert(DMUI_PAGE_KIND_SETTINGS == 1u);
static_assert(DMUI_PAGE_KIND_OVERLAY == 2u);
static_assert(DMUI_PAGE_KIND_HOME == 3u);

#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(DMUI_ImGuiFingerprint) == 216);
static_assert(sizeof(DMUI_HostReadyInfo) == 40);
static_assert(sizeof(DMUI_ClientDescriptor) == 72);
static_assert(sizeof(DMUI_PageDescriptor) == 64);
static_assert(sizeof(DMUI_HostStateInfo) == 28);
static_assert(sizeof(DMUI_HostAPI) == 80);
static_assert(DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE == sizeof(DMUI_HostAPI));
#endif
