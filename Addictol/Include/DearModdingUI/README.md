# DearModdingUI client API

`API.h` is a vendorable C ABI for Dear-Modding F4SE user interfaces. Clients link their own copy of
the pinned Dear ImGui sources and discover a host dynamically; they do not link against the host
DLL or include Addictol, CommonLibF4, F4SE, Windows, D3D, TOML, or C++ library types through this
contract.

## Discovery and registration

At F4SE `kPostPostLoad`, after every plugin `Load` has returned, locate the host DLL and resolve the
single `DMUI_GetHostAPI` export. Call it with `DMUI_API_VERSION_CURRENT`. A null result means that ABI
version is unsupported. Discovery may succeed before the host plugin initializes; `queryState` and
registration then return `DMUI_RESULT_HOST_NOT_INITIALIZED`. Export presence does not mean the
renderer is ready: register at `kPostPostLoad` and wait for exactly one lifecycle callback.

Client and page registration closes when the first valid active-swapchain `Present` begins host
initialization. Register every page immediately after the client. All descriptor strings are copied;
callback and userdata pointers must remain valid for the process lifetime. IDs use ASCII letters,
digits, `.`, `_`, and `-`. Client IDs are process-wide; page IDs are unique within their client.

Settings pages draw only inside the common modal menu. Overlay pages draw without input capture
while their reference-counted frame demand is nonzero. Balance every successful `requestFrame` with
`releaseFrame`. The common toggle controls modal visibility and game-input suppression; overlay
demand never suppresses input.

The Addictol host initializes on the first valid active-swapchain `Present` whenever any client was
accepted. Addictol's `bMenu` setting controls only registration of Addictol's own pages. External
clients remain hosted when it is false and can open the common menu with the configured toggle.

## ImGui compatibility and callbacks

The host publishes the immutable upstream commit, `IMGUI_VERSION_NUM`, docking flag, and all six
sizes used by `IMGUI_CHECKVERSION`. A client must build its expected fingerprint from its own ImGui
headers. Registration rejects any field mismatch before storing callbacks.

`onHostReady`, `onHostUnavailable`, and page draw callbacks run on the render thread. The context and
allocator functions exist only in `DMUI_HostReadyInfo`; clients must not poll for a context. In the
ready callback, set the client's statically linked ImGui globals:

```cpp
void DMUI_CALL Ready(const DMUI_HostReadyInfo* info, void*) noexcept
{
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(info->imguiContext));
	ImGui::SetAllocatorFunctions(
		info->imguiAlloc, info->imguiFree, info->imguiAllocatorUserData);
}
```

The host catches C++ exceptions and Windows structured exceptions around client callbacks, disables
a faulting page, and attempts to recover ImGui stack state. Shared-context drawing cannot provide
process isolation, so callbacks must still balance every ImGui stack operation.

If initialization fails, each accepted client receives `onHostUnavailable` with an explicit reason
and may start its standalone fallback. A client that receives `onHostReady` must stay hosted for the
process lifetime; v1 does not support runtime migration, unload, unregister, or hot reload.

## Minimal registration

```cpp
DMUI_ImGuiFingerprint fingerprint{};
fingerprint.structSize = sizeof(fingerprint);
std::memcpy(
	fingerprint.upstreamCommit,
	DMUI_IMGUI_UPSTREAM_COMMIT,
	sizeof(fingerprint.upstreamCommit));
fingerprint.imguiVersionNum = IMGUI_VERSION_NUM;
fingerprint.flags = DMUI_IMGUI_FINGERPRINT_DOCKING;
fingerprint.sizeOfImGuiIO = sizeof(ImGuiIO);
fingerprint.sizeOfImGuiStyle = sizeof(ImGuiStyle);
fingerprint.sizeOfImVec2 = sizeof(ImVec2);
fingerprint.sizeOfImVec4 = sizeof(ImVec4);
fingerprint.sizeOfImDrawVert = sizeof(ImDrawVert);
fingerprint.sizeOfImDrawIdx = sizeof(ImDrawIdx);

const auto getHost = reinterpret_cast<decltype(&DMUI_GetHostAPI)>(
	GetProcAddress(hostModule, "DMUI_GetHostAPI"));
const auto* api = getHost ? getHost(DMUI_API_VERSION_CURRENT) : nullptr;
if (!api)
{
	StartStandalone();
	return;
}

DMUI_ClientDescriptor client{
	sizeof(client),
	DMUI_API_VERSION_CURRENT,
	"example.author.mod",
	"Example Mod",
	DMUI_MAKE_VERSION(1, 0),
	&fingerprint,
	&Ready,
	&Unavailable,
	nullptr
};
DMUI_ClientHandle clientHandle{};
if (api->registerClient(&client, &clientHandle) != DMUI_RESULT_OK)
{
	StartStandalone();
	return;
}

DMUI_PageDescriptor page{
	sizeof(page),
	"settings",
	"Settings",
	"General",
	"Example settings.",
	0,
	DMUI_PAGE_KIND_SETTINGS,
	&DrawSettings,
	nullptr
};
DMUI_PageHandle pageHandle{};
if (api->registerPage(clientHandle, &page, &pageHandle) != DMUI_RESULT_OK)
{
	StartStandalone();
	return;
}
```
