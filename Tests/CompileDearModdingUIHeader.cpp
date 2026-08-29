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

#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(DMUI_ImGuiFingerprint) == 80);
static_assert(sizeof(DMUI_HostReadyInfo) == 40);
static_assert(sizeof(DMUI_ClientDescriptor) == 64);
static_assert(sizeof(DMUI_PageDescriptor) == 64);
static_assert(sizeof(DMUI_HostStateInfo) == 28);
static_assert(sizeof(DMUI_HostAPI) == 72);
#endif

