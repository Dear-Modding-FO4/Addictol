#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define DMUI_EXTERN_C extern "C"
#define DMUI_NOEXCEPT noexcept
#else
#define DMUI_EXTERN_C extern
#define DMUI_NOEXCEPT
#endif

#if defined(_MSC_VER)
#define DMUI_CALL __cdecl
#else
#define DMUI_CALL
#endif

#if defined(_WIN32) && defined(DMUI_HOST_EXPORTS)
#define DMUI_EXPORT DMUI_EXTERN_C __declspec(dllexport)
#else
#define DMUI_EXPORT DMUI_EXTERN_C
#endif

#define DMUI_MAKE_VERSION(major, minor) ((((uint32_t)(major)) << 16u) | ((uint32_t)(minor)))
#define DMUI_VERSION_MAJOR(version) ((uint32_t)(version) >> 16u)
#define DMUI_VERSION_MINOR(version) ((uint32_t)(version) & 0xFFFFu)

#define DMUI_API_VERSION_1_0 DMUI_MAKE_VERSION(1u, 0u)
#define DMUI_API_VERSION_CURRENT DMUI_API_VERSION_1_0
#define DMUI_IMGUI_UPSTREAM_COMMIT "9acdfbf46810c0c74ab281ce04122c4149ae8bd1"
#define DMUI_IMGUI_VERSION_NUM 19291u
#define DMUI_IMGUI_FINGERPRINT_DOCKING 0x00000001u
#define DMUI_IMGUI_FINGERPRINT_WCHAR32 0x00000002u
#define DMUI_IMGUI_FINGERPRINT_CUSTOM_TEXTURE_ID 0x00000004u
#define DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_VERT 0x00000008u
#define DMUI_IMGUI_FINGERPRINT_BGRA_PACKED_COLOR 0x00000010u
#define DMUI_IMGUI_FINGERPRINT_OBSOLETE_DISABLED 0x00000020u
#define DMUI_IMGUI_FINGERPRINT_TEST_ENGINE 0x00000040u
#define DMUI_IMGUI_FINGERPRINT_LEGACY_CRC32 0x00000080u
#define DMUI_IMGUI_FINGERPRINT_FREETYPE 0x00000100u
#define DMUI_IMGUI_FINGERPRINT_MATH_OPERATORS 0x00000200u
#define DMUI_IMGUI_FINGERPRINT_DEBUG_TOOLS_DISABLED 0x00000400u
#define DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_IDX 0x00000800u
#define DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_CALLBACK 0x00001000u
#define DMUI_IMGUI_FINGERPRINT_VEC2_EXTRA 0x00002000u
#define DMUI_IMGUI_FINGERPRINT_VEC4_EXTRA 0x00004000u

typedef uint32_t DMUI_Result;

#define DMUI_RESULT_OK 0u
#define DMUI_RESULT_UNSUPPORTED_ABI 1u
#define DMUI_RESULT_INVALID_ARGUMENT 2u
#define DMUI_RESULT_STRUCT_TOO_SMALL 3u
#define DMUI_RESULT_INVALID_DESCRIPTOR 4u
#define DMUI_RESULT_FINGERPRINT_MISMATCH 5u
#define DMUI_RESULT_DUPLICATE_CLIENT_ID 6u
#define DMUI_RESULT_DUPLICATE_PAGE_ID 7u
#define DMUI_RESULT_REGISTRATION_CLOSED 8u
#define DMUI_RESULT_HOST_DISABLED 9u
#define DMUI_RESULT_HOST_NOT_INITIALIZED 10u
#define DMUI_RESULT_HOST_NOT_READY 11u
#define DMUI_RESULT_BACKEND_FAILED 12u
#define DMUI_RESULT_RESOURCE_EXHAUSTED 13u
#define DMUI_RESULT_CLIENT_NOT_FOUND 14u
#define DMUI_RESULT_PAGE_NOT_FOUND 15u
#define DMUI_RESULT_INVALID_PAGE_KIND 16u
#define DMUI_RESULT_NO_FRAME_DEMAND 17u
#define DMUI_RESULT_CALLBACK_FAILED 18u
#define DMUI_RESULT_CLIENT_CAPABILITY_REQUIRED 19u
#define DMUI_RESULT_SWAPCHAIN_REJECTED 20u
#define DMUI_RESULT_RENDERER_BUSY 21u
#define DMUI_RESULT_DUPLICATE_ACTION_ID 22u
#define DMUI_RESULT_ACTION_NOT_FOUND 23u

typedef uint32_t DMUI_HostState;

#define DMUI_HOST_STATE_NOT_INITIALIZED 0u
#define DMUI_HOST_STATE_WAITING_FOR_PRESENT 1u
#define DMUI_HOST_STATE_INITIALIZING 2u
#define DMUI_HOST_STATE_READY 3u
#define DMUI_HOST_STATE_UNAVAILABLE 4u

typedef uint32_t DMUI_UnavailableReason;

#define DMUI_UNAVAILABLE_NONE 0u
#define DMUI_UNAVAILABLE_HOST_DISABLED 1u
#define DMUI_UNAVAILABLE_BACKEND_FAILED 2u

typedef uint32_t DMUI_PageKind;

#define DMUI_PAGE_KIND_SETTINGS 1u
#define DMUI_PAGE_KIND_OVERLAY 2u

typedef uint32_t DMUI_StatusSeverity;

#define DMUI_STATUS_SEVERITY_INFO 0u
#define DMUI_STATUS_SEVERITY_SUCCESS 1u
#define DMUI_STATUS_SEVERITY_WARNING 2u
#define DMUI_STATUS_SEVERITY_ERROR 3u

typedef uint32_t DMUI_ClientCapabilities;

#define DMUI_CLIENT_CAPABILITY_NONE 0u
#define DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT 0x00000001u

typedef uint64_t DMUI_ClientHandle;
typedef uint64_t DMUI_PageHandle;
typedef uint64_t DMUI_ActionHandle;

#define DMUI_INVALID_CLIENT_HANDLE ((DMUI_ClientHandle)0u)
#define DMUI_INVALID_PAGE_HANDLE ((DMUI_PageHandle)0u)
#define DMUI_INVALID_ACTION_HANDLE ((DMUI_ActionHandle)0u)

#if defined(_MSC_VER)
#pragma pack(push, 8)
#endif

typedef struct DMUI_ImGuiFingerprint
{
	uint32_t structSize;
	char upstreamCommit[41];
	uint32_t imguiVersionNum;
	uint32_t flags;
	uint32_t sizeOfImGuiIO;
	uint32_t sizeOfImGuiStyle;
	uint32_t sizeOfImVec2;
	uint32_t sizeOfImVec4;
	uint32_t sizeOfImDrawVert;
	uint32_t sizeOfImDrawIdx;
	uint32_t alignOfImGuiIO;
	uint32_t alignOfImGuiStyle;
	uint32_t alignOfImVec2;
	uint32_t alignOfImVec4;
	uint32_t alignOfImDrawVert;
	uint32_t alignOfImDrawIdx;
	uint32_t sizeOfImWchar;
	uint32_t alignOfImWchar;
	uint32_t sizeOfImTextureID;
	uint32_t alignOfImTextureID;
	uint32_t sizeOfImGuiID;
	uint32_t alignOfImGuiID;
	uint32_t sizeOfImFont;
	uint32_t alignOfImFont;
	uint32_t sizeOfImFontConfig;
	uint32_t alignOfImFontConfig;
	uint32_t sizeOfImFontGlyph;
	uint32_t alignOfImFontGlyph;
	uint32_t sizeOfImGuiContext;
	uint32_t alignOfImGuiContext;
	uint32_t sizeOfImGuiErrorRecoveryState;
	uint32_t alignOfImGuiErrorRecoveryState;
	uint32_t sizeOfImGuiNextWindowData;
	uint32_t alignOfImGuiNextWindowData;
	uint32_t sizeOfImGuiNextItemData;
	uint32_t alignOfImGuiNextItemData;
	uint32_t sizeOfImGuiPopupData;
	uint32_t alignOfImGuiPopupData;
	uint32_t offsetOfImDrawVertPos;
	uint32_t offsetOfImDrawVertUv;
	uint32_t offsetOfImDrawVertCol;
	uint64_t layoutSignature;
} DMUI_ImGuiFingerprint;

typedef void* (DMUI_CALL *DMUI_ImGuiAllocFn)(size_t size, void* userData) DMUI_NOEXCEPT;
typedef void (DMUI_CALL *DMUI_ImGuiFreeFn)(void* allocation, void* userData) DMUI_NOEXCEPT;

typedef struct DMUI_HostReadyInfo
{
	uint32_t structSize;
	uint32_t apiVersion;
	void* imguiContext;
	DMUI_ImGuiAllocFn imguiAlloc;
	DMUI_ImGuiFreeFn imguiFree;
	void* imguiAllocatorUserData;
} DMUI_HostReadyInfo;

typedef void (DMUI_CALL *DMUI_HostReadyCallback)(
	const DMUI_HostReadyInfo* info,
	void* userData);
typedef void (DMUI_CALL *DMUI_HostUnavailableCallback)(
	DMUI_UnavailableReason reason,
	void* userData);
typedef void (DMUI_CALL *DMUI_PageDrawCallback)(void* userData);
typedef void (DMUI_CALL *DMUI_ActionCallback)(void* userData);

typedef struct DMUI_ClientDescriptor
{
	uint32_t structSize;
	uint32_t apiVersion;
	const char* id;
	const char* displayName;
	uint32_t version;
	const DMUI_ImGuiFingerprint* expectedImGui;
	DMUI_HostReadyCallback onHostReady;
	DMUI_HostUnavailableCallback onHostUnavailable;
	void* userData;
	DMUI_ClientCapabilities capabilities;
} DMUI_ClientDescriptor;

typedef struct DMUI_PageDescriptor
{
	uint32_t structSize;
	const char* id;
	const char* displayName;
	const char* category;
	const char* summary;
	int32_t sortKey;
	DMUI_PageKind kind;
	DMUI_PageDrawCallback draw;
	void* userData;
} DMUI_PageDescriptor;

typedef struct DMUI_ActionDescriptor
{
	uint32_t structSize;
	const char* id;
	const char* displayLabel;
	const char* iconName;
	const char* tooltip;
	int32_t sortKey;
	DMUI_ActionCallback callback;
	void* userData;
} DMUI_ActionDescriptor;

typedef struct DMUI_HostStateInfo
{
	uint32_t structSize;
	DMUI_HostState state;
	DMUI_UnavailableReason unavailableReason;
	uint32_t registrationOpen;
	uint32_t clientCount;
	uint32_t pageCount;
	uint32_t demandedOverlayCount;
} DMUI_HostStateInfo;

typedef DMUI_Result (DMUI_CALL *DMUI_RegisterClientFn)(
	const DMUI_ClientDescriptor* descriptor,
	DMUI_ClientHandle* client) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_RegisterPageFn)(
	DMUI_ClientHandle client,
	const DMUI_PageDescriptor* descriptor,
	DMUI_PageHandle* page) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_QueryStateFn)(
	DMUI_HostStateInfo* state) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_RequestFrameFn)(
	DMUI_ClientHandle client,
	DMUI_PageHandle page) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_ReleaseFrameFn)(
	DMUI_ClientHandle client,
	DMUI_PageHandle page) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_IsMenuVisibleFn)(
	uint32_t* visible) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_SelectPageFn)(
	DMUI_ClientHandle client,
	DMUI_PageHandle page) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_AttachSwapChainFn)(
	DMUI_ClientHandle client,
	void* nativeSwapChain) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_RegisterActionFn)(
	DMUI_ClientHandle client,
	const DMUI_ActionDescriptor* descriptor,
	DMUI_ActionHandle* action) DMUI_NOEXCEPT;
typedef DMUI_Result (DMUI_CALL *DMUI_SetStatusFn)(
	DMUI_ClientHandle client,
	DMUI_StatusSeverity severity,
	const char* message) DMUI_NOEXCEPT;

typedef struct DMUI_HostAPI
{
	uint32_t structSize;
	uint32_t apiVersion;
	const DMUI_ImGuiFingerprint* imguiFingerprint;
	DMUI_RegisterClientFn registerClient;
	DMUI_RegisterPageFn registerPage;
	DMUI_QueryStateFn queryState;
	DMUI_RequestFrameFn requestFrame;
	DMUI_ReleaseFrameFn releaseFrame;
	DMUI_IsMenuVisibleFn isMenuVisible;
	DMUI_SelectPageFn selectPage;
	DMUI_AttachSwapChainFn attachSwapChain;
	DMUI_RegisterActionFn registerAction;
	DMUI_SetStatusFn setStatus;
} DMUI_HostAPI;

#define DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE \
	((uint32_t)(offsetof(DMUI_HostAPI, attachSwapChain) + sizeof(DMUI_AttachSwapChainFn)))
#define DMUI_HOST_API_REGISTER_ACTION_SIZE \
	((uint32_t)(offsetof(DMUI_HostAPI, registerAction) + sizeof(DMUI_RegisterActionFn)))
#define DMUI_HOST_API_SET_STATUS_SIZE \
	((uint32_t)(offsetof(DMUI_HostAPI, setStatus) + sizeof(DMUI_SetStatusFn)))

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

DMUI_EXPORT const DMUI_HostAPI* DMUI_CALL DMUI_GetHostAPI(
	uint32_t requestedVersion) DMUI_NOEXCEPT;
