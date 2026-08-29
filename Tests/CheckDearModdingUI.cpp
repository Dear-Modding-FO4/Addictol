#include "../Addictol/Include/DearModdingUI/Registry.h"
#include "../Addictol/Include/DearModdingUI/IconPaths.h"
#include "../Addictol/Include/DearModdingUI/ThemeDefaults.h"
#include "../Addictol/Include/DearModdingUI/VisualDecisions.h"
#include "Harness.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "../Addictol/Include/DearModdingUI/ImGuiFingerprint.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol::DearModdingUI;

		struct CallbackState
		{
			uint32_t ready{ 0 };
			uint32_t unavailable{ 0 };
			uint32_t draws{ 0 };
			DMUI_UnavailableReason reason{ DMUI_UNAVAILABLE_NONE };
			void* context{ nullptr };
		};

		[[nodiscard]] DMUI_ImGuiFingerprint Fingerprint() noexcept
		{
			return DMUI_MakeImGuiFingerprint();
		}

		[[nodiscard]] bool SameColor(
			const ImVec4& a_left,
			const ImVec4& a_right) noexcept
		{
			return a_left.x == a_right.x &&
				a_left.y == a_right.y &&
				a_left.z == a_right.z &&
				a_left.w == a_right.w;
		}

		void DMUI_CALL Ready(const DMUI_HostReadyInfo* a_info, void* a_userData) noexcept
		{
			auto& state = *static_cast<CallbackState*>(a_userData);
			++state.ready;
			state.context = a_info->imguiContext;
		}

		void DMUI_CALL Unavailable(
			DMUI_UnavailableReason a_reason,
			void* a_userData) noexcept
		{
			auto& state = *static_cast<CallbackState*>(a_userData);
			++state.unavailable;
			state.reason = a_reason;
		}

		void DMUI_CALL Draw(void* a_userData) noexcept
		{
			++static_cast<CallbackState*>(a_userData)->draws;
		}

		void DMUI_CALL ThrowReady(const DMUI_HostReadyInfo*, void*)
		{
			throw std::runtime_error("ready");
		}

		void DMUI_CALL ThrowUnavailable(DMUI_UnavailableReason, void*)
		{
			throw std::runtime_error("unavailable");
		}

		void DMUI_CALL ThrowDraw(void*)
		{
			throw std::runtime_error("draw");
		}

		[[nodiscard]] DMUI_ClientDescriptor Client(
			const char* a_id,
			const char* a_name,
			const DMUI_ImGuiFingerprint& a_fingerprint,
			CallbackState& a_state) noexcept
		{
			return {
				sizeof(DMUI_ClientDescriptor),
				DMUI_API_VERSION_CURRENT,
				a_id,
				a_name,
				DMUI_MAKE_VERSION(1, 0),
				&a_fingerprint,
				&Ready,
				&Unavailable,
				&a_state,
				DMUI_CLIENT_CAPABILITY_NONE
			};
		}

		[[nodiscard]] DMUI_PageDescriptor Page(
			const char* a_id,
			const char* a_name,
			const char* a_category,
			int32_t a_sort,
			DMUI_PageKind a_kind,
			CallbackState& a_state) noexcept
		{
			return {
				sizeof(DMUI_PageDescriptor),
				a_id,
				a_name,
				a_category,
				nullptr,
				a_sort,
				a_kind,
				&Draw,
				&a_state
			};
		}

		[[nodiscard]] DMUI_ClientHandle AddClient(
			Registry& a_registry,
			const char* a_id,
			const char* a_name,
			const DMUI_ImGuiFingerprint& a_fingerprint,
			CallbackState& a_state,
			ClientOrigin a_origin = ClientOrigin::kExternal)
		{
			auto descriptor = Client(a_id, a_name, a_fingerprint, a_state);
			DMUI_ClientHandle handle{};
			require(a_registry.RegisterClient(&descriptor, &handle, a_origin) == DMUI_RESULT_OK,
				"client registration failed");
			return handle;
		}

		[[nodiscard]] DMUI_PageHandle AddPage(
			Registry& a_registry,
			DMUI_ClientHandle a_client,
			const char* a_id,
			const char* a_name,
			const char* a_category,
			int32_t a_sort,
			DMUI_PageKind a_kind,
			CallbackState& a_state)
		{
			auto descriptor = Page(a_id, a_name, a_category, a_sort, a_kind, a_state);
			DMUI_PageHandle handle{};
			require(a_registry.RegisterPage(a_client, &descriptor, &handle) == DMUI_RESULT_OK,
				"page registration failed");
			return handle;
		}
	}

	void run_dear_modding_ui_checks(Runner& runner)
	{
		runner.test("DearModdingUI negotiates only the published ABI", [] {
			require(Registry::SupportsVersion(DMUI_API_VERSION_1_0), "v1.0 was rejected");
			require(!Registry::SupportsVersion(DMUI_MAKE_VERSION(1, 1)), "future minor was accepted");
			require(!Registry::SupportsVersion(DMUI_MAKE_VERSION(2, 0)), "future major was accepted");
			require(!Registry::SupportsVersion(0), "zero ABI was accepted");
		});

		runner.test("client descriptors reject null size and callback failures", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			DMUI_ClientHandle handle{};
			auto client = Client("sample.mod", "Sample", fingerprint, state);

			require(registry.RegisterClient(nullptr, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null descriptor was accepted");
			require(registry.RegisterClient(&client, nullptr, ClientOrigin::kExternal) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null output was accepted");
			client.structSize = sizeof(client) - 1;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_STRUCT_TOO_SMALL,
				"short client descriptor was accepted");
			client.structSize = sizeof(client);
			client.expectedImGui = nullptr;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"null fingerprint was accepted");
			client.expectedImGui = &fingerprint;
			auto shortFingerprint = fingerprint;
			shortFingerprint.structSize = sizeof(shortFingerprint) - 1;
			client.expectedImGui = &shortFingerprint;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_STRUCT_TOO_SMALL,
				"short fingerprint was accepted");
			client.expectedImGui = &fingerprint;
			client.onHostReady = nullptr;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"null ready callback was accepted");

			client.onHostReady = &Ready;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_OK,
				"valid client was rejected");
			auto page = Page("settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			DMUI_PageHandle pageHandle{};
			require(registry.RegisterPage(handle, nullptr, &pageHandle) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null page descriptor was accepted");
			require(registry.RegisterPage(handle, &page, nullptr) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null page output was accepted");
			page.structSize = sizeof(page) - 1;
			require(registry.RegisterPage(handle, &page, &pageHandle) ==
					DMUI_RESULT_STRUCT_TOO_SMALL,
				"short page descriptor was accepted");
			page.structSize = sizeof(page);
			page.kind = 99;
			require(registry.RegisterPage(handle, &page, &pageHandle) ==
					DMUI_RESULT_INVALID_PAGE_KIND,
				"unknown page kind was accepted");
			page.kind = DMUI_PAGE_KIND_SETTINGS;
			page.draw = nullptr;
			require(registry.RegisterPage(handle, &page, &pageHandle) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"null page callback was accepted");
		});

		runner.test("client fingerprint comparison is byte exact", [] {
			const auto fingerprint = Fingerprint();
			require(fingerprint.structSize == sizeof(fingerprint), "fingerprint size is stale");
			require(fingerprint.sizeOfImWchar == sizeof(ImWchar), "ImWchar size was omitted");
			require(fingerprint.sizeOfImTextureID == sizeof(ImTextureID),
				"ImTextureID size was omitted");
			require(fingerprint.sizeOfImGuiContext == sizeof(ImGuiContext),
				"ImGuiContext size was omitted");
			require(fingerprint.offsetOfImDrawVertPos == offsetof(ImDrawVert, pos) &&
					fingerprint.offsetOfImDrawVertUv == offsetof(ImDrawVert, uv) &&
					fingerprint.offsetOfImDrawVertCol == offsetof(ImDrawVert, col),
				"ImDrawVert layout was omitted");
			require(fingerprint.layoutSignature != 0, "layout signature was not constructed");
			Registry registry{ fingerprint };
			CallbackState state;
			DMUI_ClientHandle handle{};

			auto mismatch = fingerprint;
			mismatch.upstreamCommit[0] ^= 1;
			auto client = Client("sample.mod", "Sample", mismatch, state);
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"commit mismatch was accepted");
			mismatch = fingerprint;
			++mismatch.sizeOfImGuiIO;
			client.expectedImGui = &mismatch;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"layout mismatch was accepted");
			mismatch = fingerprint;
			mismatch.flags = 0;
			client.expectedImGui = &mismatch;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"docking mismatch was accepted");
			mismatch = fingerprint;
			++mismatch.sizeOfImTextureID;
			client.expectedImGui = &mismatch;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"texture ID mismatch was accepted");
			mismatch = fingerprint;
			++mismatch.offsetOfImDrawVertUv;
			client.expectedImGui = &mismatch;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"draw vertex layout mismatch was accepted");
			mismatch = fingerprint;
			mismatch.layoutSignature ^= 1;
			client.expectedImGui = &mismatch;
			require(registry.RegisterClient(&client, &handle, ClientOrigin::kExternal) ==
					DMUI_RESULT_FINGERPRINT_MISMATCH,
				"layout signature mismatch was accepted");
		});

		runner.test("swapchain handoff requires a registered renderer replacement client", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto regular = AddClient(registry, "regular.mod", "Regular", fingerprint, state);
			require(registry.ValidateSwapChainClient(regular) ==
					DMUI_RESULT_CLIENT_CAPABILITY_REQUIRED,
				"a regular client gained renderer replacement access");
			require(registry.ValidateSwapChainClient(9999) == DMUI_RESULT_CLIENT_NOT_FOUND,
				"an unknown client gained renderer replacement access");

			auto renderer = Client("renderer.mod", "Renderer", fingerprint, state);
			renderer.capabilities = DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT;
			DMUI_ClientHandle rendererHandle{};
			require(registry.RegisterClient(
						&renderer, &rendererHandle, ClientOrigin::kExternal) == DMUI_RESULT_OK,
				"renderer replacement client was rejected");
			require(registry.ValidateSwapChainClient(rendererHandle) == DMUI_RESULT_OK,
				"renderer replacement capability was not retained");

			auto unknown = Client("unknown.mod", "Unknown", fingerprint, state);
			unknown.capabilities = 0x80000000u;
			DMUI_ClientHandle unknownHandle{};
			require(registry.RegisterClient(
						&unknown, &unknownHandle, ClientOrigin::kExternal) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"unknown client capabilities were accepted");
		});

		runner.test("duplicate client and page IDs are rejected in their scopes", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto first = AddClient(registry, "a.mod", "A", fingerprint, state);
			auto duplicate = Client("a.mod", "Other", fingerprint, state);
			DMUI_ClientHandle client{};
			require(registry.RegisterClient(&duplicate, &client, ClientOrigin::kExternal) ==
					DMUI_RESULT_DUPLICATE_CLIENT_ID,
				"duplicate client ID was accepted");
			const auto second = AddClient(registry, "b.mod", "B", fingerprint, state);
			(void)AddPage(registry, first, "settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			auto page = Page("settings", "Duplicate", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			DMUI_PageHandle pageHandle{};
			require(registry.RegisterPage(first, &page, &pageHandle) ==
					DMUI_RESULT_DUPLICATE_PAGE_ID,
				"duplicate page ID in one client was accepted");
			require(registry.RegisterPage(second, &page, &pageHandle) == DMUI_RESULT_OK,
				"same page ID in another client was rejected");
		});

		runner.test("registration copies strings and grows beyond the old capacity", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			char clientId[] = "copy.mod";
			char clientName[] = "Copy";
			auto clientDescriptor = Client(clientId, clientName, fingerprint, state);
			DMUI_ClientHandle client{};
			require(registry.RegisterClient(
						&clientDescriptor, &client, ClientOrigin::kExternal) == DMUI_RESULT_OK,
				"copy client failed");
			clientId[0] = 'x';
			clientName[0] = 'X';
			for (size_t index = 0; index < 32; ++index)
			{
				const auto id = "page-" + std::to_string(index);
				const auto name = "Page " + std::to_string(index);
				(void)AddPage(registry, client, id.c_str(), name.c_str(), "General",
					static_cast<int32_t>(index), DMUI_PAGE_KIND_SETTINGS, state);
			}
			require(registry.Freeze(), "registry did not freeze");
			require(registry.PageCount() == 32, "dynamic registry retained a fixed capacity");
			require(registry.OrderedPages().front().clientId == "copy.mod",
				"client ID was not copied");
			require(registry.OrderedPages().front().clientDisplayName == "Copy",
				"client name was not copied");
		});

		runner.test("frozen pages have deterministic host category and sort ordering", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto zulu = AddClient(registry, "z.mod", "Zulu", fingerprint, state);
			const auto alpha = AddClient(
				registry, "a.host", "Alpha", fingerprint, state, ClientOrigin::kHost);
			(void)AddPage(registry, zulu, "late", "Late", "B", 20,
				DMUI_PAGE_KIND_SETTINGS, state);
			(void)AddPage(registry, alpha, "second", "Second", "B", 10,
				DMUI_PAGE_KIND_SETTINGS, state);
			(void)AddPage(registry, alpha, "first", "First", "A", 50,
				DMUI_PAGE_KIND_SETTINGS, state);
			(void)AddPage(registry, alpha, "sorted", "Sorted", "B", -10,
				DMUI_PAGE_KIND_SETTINGS, state);
			require(registry.Freeze(), "registry did not freeze");
			const auto& pages = registry.OrderedPages();
			require(pages[0].id == "first", "category ordering changed");
			require(pages[1].id == "sorted", "sort-key ordering changed");
			require(pages[2].id == "second", "host page ordering changed");
			require(pages[3].id == "late", "host pages did not precede external pages");
		});

		runner.test("navigation groups clients categories and settings pages deterministically", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto bravo = AddClient(registry, "bravo.mod", "Bravo", fingerprint, state);
			const auto alpha = AddClient(
				registry, "alpha.host", "Alpha", fingerprint, state, ClientOrigin::kHost);
			const auto alphaLate = AddPage(registry, alpha, "late", "Late", "General", 20,
				DMUI_PAGE_KIND_SETTINGS, state);
			const auto alphaEarly = AddPage(registry, alpha, "early", "Early", "General", -10,
				DMUI_PAGE_KIND_SETTINGS, state);
			(void)AddPage(registry, alpha, "advanced", "Advanced", "Advanced", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			const auto overlay = AddPage(registry, alpha, "overlay", "Overlay", "HUD", 0,
				DMUI_PAGE_KIND_OVERLAY, state);
			(void)AddPage(registry, bravo, "settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			require(registry.Freeze(), "registry did not freeze");

			const auto& navigation = registry.Navigation();
			require(navigation.clients.size() == 2, "settings clients were not grouped");
			require(navigation.clients[0].id == "alpha.host", "client order changed");
			require(navigation.clients[0].categories.size() == 2, "categories were not grouped");
			require(navigation.clients[0].categories[0].displayName == "Advanced",
				"category order changed");
			require(navigation.clients[0].categories[1].pages[0].handle == alphaEarly,
				"page sort key was ignored");
			require(navigation.clients[0].categories[1].pages[1].handle == alphaLate,
				"page sort order changed");
			require(navigation.FindPage(alphaEarly) != nullptr, "settings page was not indexed");
			require(navigation.FindPage(overlay) == nullptr,
				"overlay page entered settings navigation");
		});

		runner.test("navigation selection honors requests then keeps a stable fallback", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(registry, "selection.mod", "Selection", fingerprint, state);
			const auto first = AddPage(registry, client, "first", "First", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			const auto second = AddPage(registry, client, "second", "Second", "General", 10,
				DMUI_PAGE_KIND_SETTINGS, state);
			const auto overlay = AddPage(registry, client, "overlay", "Overlay", "HUD", 0,
				DMUI_PAGE_KIND_OVERLAY, state);
			require(registry.Freeze(), "registry did not freeze");
			const auto& navigation = registry.Navigation();
			require(ResolvePageSelection(navigation, second, first) == second,
				"requested page was not selected");
			require(ResolvePageSelection(navigation, overlay, second) == second,
				"overlay request replaced the stable selection");
			require(ResolvePageSelection(navigation, 9999, 9998) == first,
				"invalid selection did not fall back to the first page");
		});

		runner.test("icon names and paths resolve by the shared convention", [] {
			const std::filesystem::path root{ "Data/F4SE/Plugins/DearModdingUI/Icons" };
			require(SlugifyIconName("Post Process") == "post-process",
				"spaces were not collapsed");
			require(SlugifyIconName("Mixed___CASE Name") == "mixed-case-name",
				"underscores or mixed case changed");
			require(SlugifyIconName("A.B/C-D!") == "abcd",
				"punctuation was not dropped");
			require(SlugifyIconName("").empty() && SlugifyIconName("!@#$").empty(),
				"empty icon names produced a slug");

			const auto category = BuildIconPath(root, IconKind::kCategory, "Post Process");
			const auto punctuated =
				BuildIconPath(root, IconKind::kCategory, "Post-process");
			const auto client = BuildIconPath(
				root, IconKind::kClient, "dear-modding.addictol");
			require(category == root / "Categories" / "post-process.png",
				"category icon path changed");
			require(punctuated == root / "Categories" / "postprocess.png",
				"punctuated category icon path changed");
			require(client == root / "Clients" / "dearmoddingaddictol.png",
				"client icon path changed");
			require(!BuildIconPath(root, IconKind::kCategory, "..."),
				"empty category slug produced a path");

			const auto resolved = ResolveIconPath(
				root,
				IconKind::kCategory,
				"Post Process",
				[&](const auto& a_path) { return a_path == *category; });
			const auto missing = ResolveIconPath(
				root,
				IconKind::kCategory,
				"No Matching File",
				[](const auto&) { return false; });
			require(resolved == category, "existing icon did not resolve");
			require(!missing, "missing icon resolved to a blank resource");
		});

		runner.test("client dropdown selection handles zero one and many clients", [] {
			ClientSelectionState selection{
				DMUI_INVALID_CLIENT_HANDLE,
				DMUI_INVALID_PAGE_HANDLE,
				"unchanged"
			};
			const NavigationModel empty;
			require(!SelectClient(empty, 1, selection),
				"zero-client selection unexpectedly changed");
			require(selection.search == "unchanged",
				"zero-client selection cleared the search");

			NavigationModel single;
			single.clients.push_back({
				1,
				"single.mod",
				"Single",
				DMUI_MAKE_VERSION(1, 0),
				{ NavigationCategory{
					"General",
					{ NavigationPage{
						10,
						1,
						"only",
						"Only",
						"General",
						{},
						0 } } } }
			});
			require(SelectClient(single, 1, selection),
				"single client could not be selected");
			require(selection.activeClient == 1 && selection.activePage == 10,
				"single client did not select its first page");
			require(selection.search.empty(),
				"single-client selection did not clear search");
			selection.search = "keep";
			require(!SelectClient(single, 1, selection) && selection.search == "keep",
				"reselecting the active client changed state");

			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState callback;
			const auto zulu = AddClient(
				registry, "z.external", "Zulu", fingerprint, callback);
			const auto alpha = AddClient(
				registry,
				"alpha.host",
				"Alpha",
				fingerprint,
				callback,
				ClientOrigin::kHost);
			const auto zuluPage = AddPage(
				registry, zulu, "settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, callback);
			const auto alphaPage = AddPage(
				registry, alpha, "settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, callback);
			require(registry.Freeze(), "many-client registry did not freeze");
			const auto& many = registry.Navigation();
			require(many.clients.size() == 2 &&
					many.clients[0].handle == alpha &&
					many.clients[1].handle == zulu,
				"client dropdown order was not deterministic");

			selection = { alpha, alphaPage, "pages" };
			require(SelectClient(many, zulu, selection),
				"many-client selection did not change");
			require(selection.activeClient == zulu &&
					selection.activePage == zuluPage &&
					selection.search.empty(),
				"selection change did not reset page and search");
		});

		runner.test("one-page navigation and failed-page presentation remain stable", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(registry, "single.mod", "Single", fingerprint, state);
			const auto page = AddPage(registry, client, "only", "Only", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			require(registry.Freeze(), "registry did not freeze");
			const auto& navigation = registry.Navigation();
			require(navigation.clients.size() == 1, "single client was omitted");
			require(navigation.clients[0].categories.size() == 1, "single category was omitted");
			require(navigation.FirstPage() == page, "single page was not the fallback");
			require(DecidePagePresentation(navigation.FindPage(page), false) ==
					PagePresentation::kContent,
				"healthy page did not present content");
			require(DecidePagePresentation(navigation.FindPage(page), true) ==
					PagePresentation::kFailure,
				"failed page did not present a stable error");
			registry.MarkPageFailed(page);
			require(registry.PageFailed(page), "failed page state was not retained");
			require(registry.HasSettingsPages(), "failed page removed the host's settings shell");
			require(DecidePagePresentation(nullptr, false) == PagePresentation::kEmpty,
				"missing page did not present an empty state");
		});

		runner.test("Community Shaders style scalars are independently pinned", [] {
			const auto& style = Theme::kStyleDefaults;
			require(style.windowBorderSize == 2.0f, "window border changed");
			require(style.childBorderSize == 0.0f, "child border changed");
			require(style.frameBorderSize == 1.0f, "frame border changed");
			require(style.windowPadding.x == 8.0f && style.windowPadding.y == 8.0f,
				"window padding changed");
			require(style.windowRounding == 12.0f, "window rounding changed");
			require(style.indentSpacing == 8.0f, "indent spacing changed");
			require(style.framePadding.x == 8.0f && style.framePadding.y == 4.0f,
				"frame padding changed");
			require(style.cellPadding.x == 8.0f && style.cellPadding.y == 2.0f,
				"cell padding changed");
			require(style.itemSpacing.x == 4.0f && style.itemSpacing.y == 8.0f,
				"item spacing changed");
			require(style.frameRounding == 4.0f, "frame rounding changed");
			require(style.tabRounding == 4.0f, "tab rounding changed");
			require(style.scrollbarRounding == 9.0f, "scrollbar rounding changed");
			require(style.scrollbarSize == 12.0f, "scrollbar size changed");
			require(style.grabRounding == 3.0f, "grab rounding changed");
			require(style.grabMinSize == 12.0f, "grab size changed");
			require(Theme::kScrollbarOpacityDefaults.background == 0.0f &&
					Theme::kScrollbarOpacityDefaults.thumb == 0.5f &&
					Theme::kScrollbarOpacityDefaults.thumbHovered == 0.75f &&
					Theme::kScrollbarOpacityDefaults.thumbActive == 0.9f,
				"scrollbar opacity changed");
			require(Theme::kTooltipHoverDelay == 0.1f, "tooltip delay changed");
			require(Theme::kFeatureHeadingDefaults.titleScale == 1.5f &&
					Theme::kFeatureHeadingDefaults.minimizedFactor == 0.7f,
				"feature heading defaults changed");
			require(SameColor(
						Theme::kStatusPaletteDefaults.disable,
						{ 0.5f, 0.5f, 0.5f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.error,
						{ 1.0f, 0.4f, 0.4f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.warning,
						{ 1.0f, 0.6f, 0.2f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.restartNeeded,
						{ 0.4f, 1.0f, 0.4f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.currentHotkey,
						{ 1.0f, 1.0f, 0.0f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.success,
						{ 0.0f, 1.0f, 0.0f, 1.0f }) &&
					SameColor(
						Theme::kStatusPaletteDefaults.info,
						{ 0.2f, 1.0f, 0.328f, 1.0f }),
				"status palette changed");

			const auto applied = Theme::MakeBaseStyle();
			require(applied.WindowBorderSize == style.windowBorderSize &&
					applied.ChildBorderSize == style.childBorderSize &&
					applied.FrameBorderSize == style.frameBorderSize &&
					applied.WindowPadding.x == style.windowPadding.x &&
					applied.WindowPadding.y == style.windowPadding.y &&
					applied.WindowRounding == style.windowRounding &&
					applied.IndentSpacing == style.indentSpacing &&
					applied.FramePadding.x == style.framePadding.x &&
					applied.FramePadding.y == style.framePadding.y &&
					applied.CellPadding.x == style.cellPadding.x &&
					applied.CellPadding.y == style.cellPadding.y &&
					applied.ItemSpacing.x == style.itemSpacing.x &&
					applied.ItemSpacing.y == style.itemSpacing.y &&
					applied.FrameRounding == style.frameRounding &&
					applied.TabRounding == style.tabRounding &&
					applied.ScrollbarRounding == style.scrollbarRounding &&
					applied.ScrollbarSize == style.scrollbarSize &&
					applied.GrabRounding == style.grabRounding &&
					applied.GrabMinSize == style.grabMinSize,
				"base style application diverged from pinned scalars");
		});

		runner.test("Community Shaders full palette is independently pinned", [] {
			const std::array<ImVec4, ImGuiCol_COUNT> expected{
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
				ImVec4(1.0f, 1.0f, 1.0f, 0.3f),
				ImVec4(0.03f, 0.03f, 0.03f, 0.55f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
				ImVec4(0.05f, 0.05f, 0.1f, 0.85f),
				ImVec4(0.5f, 0.5f, 0.5f, 0.8f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
				ImVec4(0.4f, 0.4f, 0.4f, 0.7f),
				ImVec4(0.26f, 0.26f, 0.26f, 0.4f),
				ImVec4(0.4f, 0.4f, 0.4f, 0.45f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.83f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.87f),
				ImVec4(0.2f, 0.2f, 0.3f, 0.9f),
				ImVec4(0.02f, 0.02f, 0.03f, 0.9f),
				ImVec4(0.2f, 0.22f, 0.27f, 0.9f),
				ImVec4(0.28f, 0.28f, 0.28f, 1.0f),
				ImVec4(0.42f, 0.42f, 0.42f, 1.0f),
				ImVec4(0.56f, 0.56f, 0.56f, 1.0f),
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
				ImVec4(0.31f, 0.31f, 0.31f, 0.5f),
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
				ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.39f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.2f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),
				ImVec4(0.06f, 0.98f, 0.2072f, 0.39f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.2f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),
				ImVec4(0.5f, 0.5f, 0.5f, 0.6f),
				ImVec4(0.7f, 0.6f, 0.6f, 1.0f),
				ImVec4(0.9f, 0.7f, 0.7f, 1.0f),
				ImVec4(0.6f, 0.6f, 0.6f, 0.8f),
				ImVec4(0.6f, 0.6f, 0.6f, 0.1f),
				ImVec4(0.6f, 0.6f, 0.6f, 0.1f),
				ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.31f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.8f),
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
				ImVec4(0.38f, 0.83f, 0.452f, 1.0f),
				ImVec4(0.15f, 0.15f, 0.15f, 0.97f),
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
				ImVec4(0.5f, 0.5f, 0.5f, 0.0f),
				ImVec4(0.7f, 0.6f, 0.6f, 0.5f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
				ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.4f),
				ImVec4(0.26f, 0.26f, 0.26f, 1.0f),
				ImVec4(0.19f, 0.19f, 0.19f, 1.0f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
				ImVec4(1.0f, 1.0f, 1.0f, 0.06f),
				ImVec4(0.38f, 0.83f, 0.452f, 1.0f),
				ImVec4(0.26f, 0.98f, 0.3752f, 0.35f),
				ImVec4(0.7f, 0.7f, 0.7f, 0.65f),
				ImVec4(0.8f, 0.5f, 0.5f, 1.0f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
				ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
				ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
				ImVec4(0.3f, 0.3f, 0.3f, 0.56f),
				ImVec4(0.2f, 0.2f, 0.2f, 0.35f),
				ImVec4(0.2f, 0.2f, 0.2f, 0.35f)
			};
			require(expected.size() == Theme::kFullPalette.size(),
				"palette size changed");
			for (size_t index = 0; index < expected.size(); ++index)
			{
				require(SameColor(expected[index], Theme::kFullPalette[index]),
					"palette entry changed");
			}
			const auto effective = Theme::MakeEffectivePalette();
			require(effective[ImGuiCol_ScrollbarBg].w == 0.0f &&
					effective[ImGuiCol_ScrollbarGrab].w == 0.5f &&
					effective[ImGuiCol_ScrollbarGrabHovered].w == 0.75f &&
					effective[ImGuiCol_ScrollbarGrabActive].w == 0.9f,
				"effective scrollbar opacity changed");
		});

		runner.test("Community Shaders font roles and scaling stay exact", [] {
			require(Theme::kFontRoleDefaults.size() == 5, "font role count changed");
			require(Theme::kFontRoleDefaults[0].family == "Jost" &&
					Theme::kFontRoleDefaults[0].style == "Regular" &&
					Theme::kFontRoleDefaults[0].file == "Jost\\Jost-Regular.ttf" &&
					Theme::kFontRoleDefaults[0].sizeScale == 1.0f,
				"body role changed");
			require(Theme::kFontRoleDefaults[1].family == "Jost" &&
					Theme::kFontRoleDefaults[1].style == "SemiBold" &&
					Theme::kFontRoleDefaults[1].file == "Jost\\Jost-SemiBold.ttf" &&
					Theme::kFontRoleDefaults[1].sizeScale == 1.3f,
				"title role changed");
			require(Theme::kFontRoleDefaults[2].sizeScale == 1.0f &&
					Theme::kFontRoleDefaults[3].sizeScale == 1.0f &&
					Theme::kFontRoleDefaults[4].sizeScale == 0.9f,
				"secondary font roles changed");
			require(Theme::ResolveFontSize(1080) == 21.0f, "1080p font changed");
			require(Theme::ResolveRoleFontSize(Theme::FontRole::kTitle, 1080) == 27.0f,
				"title point scale changed");
			require(Theme::ResolveRoleFontSize(Theme::FontRole::kSubtext, 1080) == 19.0f,
				"subtext point scale changed");
			require(Theme::ResolveFontSize(720) == 16.0f, "minimum font size changed");
			require(Theme::ResolveFontSize(2160) == 42.0f, "4K font size changed");
			require(Theme::ResolveFontSize(8640) == 108.0f, "maximum font size changed");
			require(ResolveUiScale(1.0f, 1080) == 1.0f, "1080p UI scale changed");
			require(ResolveUiScale(2.0f, 1080) == 1.0f, "DPI altered CS scaling");
			require(ResolveUiScale(1.0f, 2160) == 2.0f, "4K UI scale changed");
			require(Theme::ResolveStyleScale(21.0f, 0.0f) == 1.0f,
				"default global scale changed");
			require(Theme::ResolveStyleScale(21.0f, 1.0f) == 2.0f,
				"exponential global scale changed");
			require(!Theme::kCursorDefaults.useCustomCursor &&
					Theme::kCursorDefaults.scale == 1.0f,
				"default cursor metadata changed");
			for (const auto& cursor : Theme::kCursorDefaults.types)
			{
				require(cursor.file.empty() &&
						cursor.hotspotX == 0.0f &&
						cursor.hotspotY == 0.0f,
					"default cursor image metadata changed");
			}
		});

		runner.test("absent icons reserve no navigation layout space", [] {
			const auto absent = DecideInlineIconLayout(false, 80.0f, 20.0f, 20.0f, 4.0f);
			require(!absent.drawIcon &&
					absent.iconSize == 0.0f &&
					absent.textOffset == 0.0f &&
					absent.contentWidth == 80.0f &&
					absent.contentHeight == 20.0f,
				"absent icon left a blank layout box");

			const auto present = DecideInlineIconLayout(true, 80.0f, 18.0f, 20.0f, 4.0f);
			require(present.drawIcon &&
					present.iconSize == 20.0f &&
					present.textOffset == 24.0f &&
					present.contentWidth == 104.0f &&
					present.contentHeight == 20.0f,
				"present icon layout did not align to the font");
		});

		runner.test("cursor ownership follows modal visibility", [] {
			const auto overlay = DecideCursorPresentation(false, false);
			require(!overlay.captureInput &&
					!overlay.hideOperatingSystemCursor &&
					!overlay.drawSoftwareCursor &&
					!overlay.drawCustomCursor,
				"overlay-only drawing acquired a cursor");

			const auto modal = DecideCursorPresentation(true, false);
			require(modal.captureInput &&
					modal.hideOperatingSystemCursor &&
					modal.drawSoftwareCursor &&
					!modal.drawCustomCursor,
				"modal drawing did not own exactly one software cursor");
			require(
				static_cast<uint32_t>(modal.drawSoftwareCursor) +
						static_cast<uint32_t>(modal.drawCustomCursor) ==
					1,
				"the virtual branch did not present exactly one cursor");

			const auto shared = DecideCursorPresentation(true, true);
			require(shared.captureInput &&
					shared.hideOperatingSystemCursor &&
					!shared.drawSoftwareCursor &&
					!shared.drawCustomCursor,
				"a visible Fallout cursor was duplicated by ImGui");
			require(
				1 +
						static_cast<uint32_t>(shared.drawSoftwareCursor) +
						static_cast<uint32_t>(shared.drawCustomCursor) ==
					1,
				"the engine branch did not present exactly one cursor");

			require(DecideCursorTransition(false, true) ==
					CursorOwnershipTransition::kAcquire,
				"menu open did not acquire cursor ownership");
			require(DecideCursorTransition(true, false) ==
					CursorOwnershipTransition::kRelease,
				"menu close did not release cursor ownership");
			require(DecideCursorTransition(true, true) ==
					CursorOwnershipTransition::kNone,
				"steady modal state retriggered ownership");
			require(DecideCursorTransition(false, false) ==
					CursorOwnershipTransition::kNone,
				"steady overlay state changed ownership");
		});

		runner.test("registry freeze rejects late clients and pages", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(registry, "freeze.mod", "Freeze", fingerprint, state);
			require(registry.Freeze(), "registry did not freeze");
			auto lateClient = Client("late.mod", "Late", fingerprint, state);
			DMUI_ClientHandle clientHandle{};
			require(registry.RegisterClient(
						&lateClient, &clientHandle, ClientOrigin::kExternal) ==
					DMUI_RESULT_REGISTRATION_CLOSED,
				"late client was accepted");
			auto latePage = Page("late", "Late", "General", 0, DMUI_PAGE_KIND_SETTINGS, state);
			DMUI_PageHandle pageHandle{};
			require(registry.RegisterPage(client, &latePage, &pageHandle) ==
					DMUI_RESULT_REGISTRATION_CLOSED,
				"late page was accepted");
		});

		runner.test("ready and unavailable notifications happen exactly once", [] {
			const auto fingerprint = Fingerprint();
			CallbackState readyState;
			Registry readyRegistry{ fingerprint };
			(void)AddClient(readyRegistry, "ready.mod", "Ready", fingerprint, readyState);
			require(readyRegistry.Freeze(), "ready registry did not freeze");
			const DMUI_HostReadyInfo info{
				sizeof(DMUI_HostReadyInfo),
				DMUI_API_VERSION_CURRENT,
				reinterpret_cast<void*>(0x1234),
				nullptr,
				nullptr,
				nullptr
			};
			readyRegistry.NotifyReady(info);
			readyRegistry.NotifyReady(info);
			readyRegistry.NotifyUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
			require(readyState.ready == 1 && readyState.unavailable == 0,
				"ready client received duplicate or mixed notifications");
			require(readyState.context == info.imguiContext, "ready context changed");

			CallbackState unavailableState;
			Registry unavailableRegistry{ fingerprint };
			(void)AddClient(
				unavailableRegistry, "fallback.mod", "Fallback", fingerprint, unavailableState);
			require(unavailableRegistry.Freeze(), "unavailable registry did not freeze");
			unavailableRegistry.NotifyUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
			unavailableRegistry.NotifyUnavailable(DMUI_UNAVAILABLE_HOST_DISABLED);
			unavailableRegistry.NotifyReady(info);
			require(unavailableState.ready == 0 && unavailableState.unavailable == 1,
				"unavailable client received duplicate or mixed notifications");
			require(unavailableState.reason == DMUI_UNAVAILABLE_BACKEND_FAILED,
				"unavailable reason changed");
		});

		runner.test("throwing client callbacks are isolated by host guards", [] {
			const auto fingerprint = Fingerprint();
			const DMUI_HostReadyInfo info{
				sizeof(DMUI_HostReadyInfo),
				DMUI_API_VERSION_CURRENT,
				reinterpret_cast<void*>(0x1234),
				nullptr,
				nullptr,
				nullptr
			};

			CallbackState readyState;
			Registry readyRegistry{ fingerprint };
			auto readyClient = Client("throw-ready.mod", "Throw Ready", fingerprint, readyState);
			readyClient.onHostReady = &ThrowReady;
			DMUI_ClientHandle readyHandle{};
			require(readyRegistry.RegisterClient(
						&readyClient, &readyHandle, ClientOrigin::kExternal) == DMUI_RESULT_OK,
				"throwing ready client was not registered");
			const auto readyPage = AddPage(
				readyRegistry,
				readyHandle,
				"settings",
				"Settings",
				"General",
				0,
				DMUI_PAGE_KIND_SETTINGS,
				readyState);
			require(readyRegistry.Freeze(), "throwing ready registry did not freeze");
			readyRegistry.NotifyReady(info);
			require(readyRegistry.PageFailed(readyPage),
				"a client with a throwing ready callback remained drawable");

			CallbackState drawState;
			Registry drawRegistry{ fingerprint };
			const auto drawClient = AddClient(
				drawRegistry, "throw-draw.mod", "Throw Draw", fingerprint, drawState);
			auto drawPageDescriptor = Page(
				"settings", "Settings", "General", 0, DMUI_PAGE_KIND_SETTINGS, drawState);
			drawPageDescriptor.draw = &ThrowDraw;
			DMUI_PageHandle drawPage{};
			require(drawRegistry.RegisterPage(
						drawClient, &drawPageDescriptor, &drawPage) == DMUI_RESULT_OK,
				"throwing draw page was not registered");
			require(drawRegistry.InvokePage(drawPage) == DMUI_RESULT_CALLBACK_FAILED,
				"a throwing page escaped its host guard");
			require(drawRegistry.InvokePage(drawPage) == DMUI_RESULT_CALLBACK_FAILED,
				"a faulted page was invoked again");

			CallbackState unavailableState;
			CallbackState healthyState;
			Registry unavailableRegistry{ fingerprint };
			auto unavailableClient = Client(
				"throw-unavailable.mod", "Throw Unavailable", fingerprint, unavailableState);
			unavailableClient.onHostUnavailable = &ThrowUnavailable;
			DMUI_ClientHandle unavailableHandle{};
			require(unavailableRegistry.RegisterClient(
						&unavailableClient,
						&unavailableHandle,
						ClientOrigin::kExternal) == DMUI_RESULT_OK,
				"throwing unavailable client was not registered");
			(void)AddClient(
				unavailableRegistry, "healthy.mod", "Healthy", fingerprint, healthyState);
			require(unavailableRegistry.Freeze(), "unavailable registry did not freeze");
			unavailableRegistry.NotifyUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
			require(healthyState.unavailable == 1,
				"a throwing unavailable callback blocked the next client");
		});

		runner.test("overlay frame demand is reference counted and never makes settings demand frames", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(registry, "frames.mod", "Frames", fingerprint, state);
			const auto settings = AddPage(registry, client, "settings", "Settings", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			const auto overlay = AddPage(registry, client, "overlay", "Overlay", "HUD", 0,
				DMUI_PAGE_KIND_OVERLAY, state);
			require(registry.RequestFrame(client, settings) == DMUI_RESULT_INVALID_PAGE_KIND,
				"settings page requested overlay frames");
			require(registry.RequestFrame(client, overlay) == DMUI_RESULT_OK,
				"first overlay request failed");
			require(registry.RequestFrame(client, overlay) == DMUI_RESULT_OK,
				"second overlay request failed");
			require(registry.DemandedOverlayCount() == 1, "one overlay counted twice");
			require(registry.ReleaseFrame(client, overlay) == DMUI_RESULT_OK,
				"first overlay release failed");
			require(registry.IsFrameDemanded(overlay), "one release cleared two requests");
			require(registry.ReleaseFrame(client, overlay) == DMUI_RESULT_OK,
				"second overlay release failed");
			require(!registry.IsFrameDemanded(overlay), "balanced releases left demand");
			require(registry.ReleaseFrame(client, overlay) == DMUI_RESULT_NO_FRAME_DEMAND,
				"unbalanced release was accepted");
			require(registry.HasSettingsPages(), "overlay behavior hid modal settings");
		});

		runner.test("page callbacks receive userdata and failed lookups stay isolated", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(registry, "draw.mod", "Draw", fingerprint, state);
			const auto page = AddPage(registry, client, "draw", "Draw", "General", 0,
				DMUI_PAGE_KIND_SETTINGS, state);
			require(registry.InvokePage(page) == DMUI_RESULT_OK, "draw callback failed");
			require(state.draws == 1, "draw callback did not receive userdata");
			require(registry.InvokePage(page + 1) == DMUI_RESULT_PAGE_NOT_FOUND,
				"unknown page was invoked");
		});
	}
}
