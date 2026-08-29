# DearModdingUI client API

`API.h` is a vendorable C ABI for Dear-Modding F4SE user interfaces. `ImGuiFingerprint.h` is the
optional C++ fingerprint builder. Clients link their own copy of the pinned Dear ImGui sources and
discover a host dynamically; they do not link against the host DLL or include Addictol, CommonLibF4,
F4SE, Windows, D3D, TOML, or C++ library types through the C contract.

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
Set only documented `DMUI_ClientDescriptor::capabilities`; unknown bits reject the descriptor.

Settings pages draw only inside the common modal menu. Overlay pages draw without input capture
while their reference-counted frame demand is nonzero. Balance every successful `requestFrame` with
`releaseFrame`. The common toggle controls modal visibility and game-input suppression; overlay
demand never suppresses input.

## Shared menu

The Dear Modding window owns all navigation chrome. Its mod selector is built from registered client
display names; the sidebar then groups that client's settings pages by category and orders pages by
`sortKey`, display name, and ID. Overlay pages never appear there. `selectPage` switches both the
active mod and page, opens the window, and falls back deterministically if the previous selection is
not available.

Clients receive a clean scrolling content region below the host-owned page title, category, and
summary. Draw regular ImGui controls there. Do not begin independent top-level windows, draw over
the sidebar/header, change the host style or fonts, or retain pointers into host navigation data.
Client pages inherit the active theme and may use their own balanced child regions and popups.

The host ports Community Shaders' current default palette, style dimensions, Jost Body, Title,
Heading, Subheading, and Subtext roles, resolution scaling, search and navigation treatments,
rounded title-bar highlights, footer, docking, and background blur around the neutral registry.
Layout is saved to `Data\F4SE\Plugins\DearModdingUI\imgui.ini`. Fonts and blur shaders load only from
that neutral root. Missing fonts fall back by role to ImGui's built-in font; missing or invalid blur
shaders disable blur without disabling the menu or the C ABI host.

The modal host uses Community Shaders' default ImGui software cursor path, defers to Fallout when its
native menu cursor is already open, and suppresses the Win32 cursor while modal. It releases cursor
ownership through the game window procedure when closed. Overlay-only frames do not draw a cursor or
capture input.

The Addictol host initializes on the first valid active-swapchain `Present` whenever any client was
accepted. Addictol's `bMenu` setting controls only registration of Addictol's own pages. External
clients remain hosted when it is false and can open the common menu with the configured toggle.

## Final swapchain handoff

A client that replaces the renderer's swapchain declares
`DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT` when registering. After it publishes the final native
swapchain, it may call the optional `attachSwapChain(clientHandle, nativeSwapChain)` entry. Check
`DMUI_HostAPI::structSize >= DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE` and that the pointer is non-null
before calling it. On Windows/D3D11, `nativeSwapChain` is an `IDXGISwapChain*`; the public ABI keeps it
opaque and exposes no D3D types.

The host validates the client handle and capability, validates the swapchain's D3D11 device, immediate
context, and output window, installs its final `Present`/`ResizeBuffers` dispatch, and retains its own
COM references. Attachment is allowed while waiting for the first `Present` and after the host is
ready. A ready retarget keeps the shared ImGui context and safely reinitializes the platform/renderer
backends only if the render binding changed. An attachment racing backend initialization returns
`DMUI_RESULT_RENDERER_BUSY`; an invalid or unhookable native object returns
`DMUI_RESULT_SWAPCHAIN_REJECTED`. Regular clients receive
`DMUI_RESULT_CLIENT_CAPABILITY_REQUIRED`.

Discovery ignores unrelated swapchains while an attachment is active. Destruction of the active
window or a definitive DXGI device loss retires the attachment, releases host-owned COM/resources,
and permits the next discovered or explicit final swapchain to attach.

## ImGui compatibility and callbacks

The host publishes the immutable upstream commit, `IMGUI_VERSION_NUM`, explicit compile-configuration
flags, size and alignment fields for shared public/internal types, `ImDrawVert` member offsets, and a
deterministic layout signature. The signature is built from `sizeof`, `alignof`, and `offsetof`
expressions over public draw, font, IO, style, platform, context, and recovery structures. It is never
a copied magic value. A client must build its expected fingerprint from the exact headers and
configuration used to compile its own ImGui sources. Registration rejects any field mismatch before
storing callbacks.

Include the pinned `imgui.h` and `imgui_internal.h`, then vendored `ImGuiFingerprint.h`, and call
`DMUI_MakeImGuiFingerprint()`. The builder derives custom `ImTextureID`, `ImDrawIdx`, callback,
`ImDrawVert`, `ImWchar`, color packing, docking, obsolete API, test-engine, CRC, FreeType, debug-tool,
math-operator, and vector-extension flags directly from the active preprocessor configuration.

`onHostReady`, `onHostUnavailable`, and page draw callbacks run on the render thread. The context and
allocator functions exist only in `DMUI_HostReadyInfo`; clients must not poll for a context. In the
ready callback, set the client's statically linked ImGui globals:

```cpp
void DMUI_CALL Ready(const DMUI_HostReadyInfo* info, void*)
{
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(info->imguiContext));
	ImGui::SetAllocatorFunctions(
		info->imguiAlloc, info->imguiFree, info->imguiAllocatorUserData);
}
```

Client callback typedefs are intentionally not `noexcept`, so a C++ exception reaches the host guard
instead of terminating the process. Host API entry points and allocator callbacks remain `noexcept`.
The host catches C++ exceptions and Windows structured exceptions around client callbacks, disables a
faulting page, recovers the pinned ImGui stack state, and keeps a stable error entry in navigation.
Shared-context drawing cannot provide process isolation, so callbacks must still balance every ImGui
stack operation.

If initialization fails, each accepted client receives `onHostUnavailable` with an explicit reason
and may start its standalone fallback. A client that receives `onHostReady` must stay hosted for the
process lifetime; v1 does not support runtime migration, unload, unregister, or hot reload.

## Minimal registration

```cpp
// Include imgui.h and imgui_internal.h before the fingerprint builder.
const auto fingerprint = DMUI_MakeImGuiFingerprint();

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
	nullptr,
	DMUI_CLIENT_CAPABILITY_NONE
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

A renderer-replacing client sets `client.capabilities` to
`DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT`, stores the returned API table and client handle, then
hands off its final published proxy:

```cpp
if (api->structSize < DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE ||
	!api->attachSwapChain ||
	api->attachSwapChain(clientHandle, finalSwapChain) != DMUI_RESULT_OK)
{
	ReportHandoffFailure();
}
```
