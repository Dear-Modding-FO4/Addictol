#include "../Addictol/Include/DearModdingUI/Registry.h"
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

		runner.test("visual scale resolves DPI and resolution with safe bounds", [] {
			require(ResolveUiScale(1.0f, 1080) == 1.0f, "1080p scale changed");
			require(ResolveUiScale(1.5f, 1080) == 1.5f, "DPI scale was ignored");
			require(ResolveUiScale(1.0f, 2160) == 2.0f, "4K scale was ignored");
			require(ResolveUiScale(0.0f, 0) == 1.0f, "invalid inputs did not fall back");
			require(ResolveUiScale(4.0f, 4320) == 2.5f, "scale maximum was not enforced");
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
