# DearModdingUI client API

`API.h` is a vendorable C ABI for Dear-Modding F4SE user interfaces. `ImGuiFingerprint.h` is the
optional C++ fingerprint builder. Clients link their own copy of the pinned Dear ImGui sources and
discover a host dynamically; they do not link against the host DLL or include Addictol, CommonLibF4,
F4SE, Windows, D3D, TOML, or C++ library types through the C contract.
Vendors should copy the current `API.h` as a unit. The additive `DMUI_PAGE_KIND_HOME` constant in
the current 1.0 header changes neither an existing constant value nor any structure layout.

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

Home and settings pages draw only inside the common modal menu. Each client may register at most one
home page; a second returns `DMUI_RESULT_DUPLICATE_PAGE_ID`. A home descriptor may leave `category`
null because home is not grouped into a category. Overlay pages draw without input capture while
their reference-counted frame demand is nonzero. Balance every successful `requestFrame` with
`releaseFrame`. Home and settings pages reject frame demand. The common toggle controls modal
visibility and game-input suppression; overlay demand never suppresses input.

## Shared menu

The Evil Modding window owns all navigation chrome. Its mod dropdown is built from registered client
display names. A client's home page appears first as its landing page, followed by settings pages
grouped by category and ordered by `sortKey`, display name, and ID. Switching mods selects home.
When a client registers no home page, the host adds one showing its display name, version, category
and page counts, and the current failed-page count. Overlay pages never appear there. `selectPage`
accepts home and settings pages, switches both the active mod and page, opens the window, and falls
back deterministically if the previous selection is not available.

Clients receive a clean scrolling content region below the host-owned page title, category, and
summary. Draw regular ImGui controls there. Do not begin independent top-level windows, draw over
the sidebar/header, change the host style or fonts, or retain pointers into host navigation data.
Client pages inherit the active theme and may use their own balanced child regions and popups.

The host ports Community Shaders' current default palette, style dimensions, Jost Body, Title,
Heading, Subheading, and Subtext roles, resolution scaling, search and navigation treatments,
rounded title-bar highlights, footer, docking, and background blur around the neutral registry.
Layout is saved to `Data\F4SE\Plugins\DearModdingUI\imgui.ini`. Fonts, icons, and blur shaders load
only from that neutral root. Client IDs and category names select Phosphor glyphs from an in-code
table after lowercase slug normalization. Icons use the accent tint by default. The title-bar gear
opens host-only interface settings for colored or monochrome icons and background blur without adding
an entry to the mod dropdown. The same popup reports the configured toggle key and refresh interval
alongside the resolved typography size and UI scale. Editable values use Addictol's `[Additional]`
TOML settings. A missing icon font falls back to text-only labels without disabling the menu or the
C ABI host.
When a normalized category name equals its client's normalized display name or full client ID, the
category inherits that client's glyph.

The modal host opens a registered, hidden Fallout 4 carrier menu so absolute client coordinates remain
valid, then maps them into the attached backbuffer. The carrier movie and operating-system cursor stay
hidden while ImGui draws the only visible pointer. Closing the modal host releases Win32 cursor
ownership and removes the carrier from the menu stack. Overlay-only frames do not draw a cursor,
capture input, or open the carrier.

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

Use `DMUI_PAGE_KIND_HOME` with a null category for a custom landing page. Omitting it asks the host
to synthesize the default landing page during registration freeze.

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
