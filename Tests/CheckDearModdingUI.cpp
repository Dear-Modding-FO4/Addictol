#include "../Addictol/Include/DearModdingUI/Registry.h"
#include "../Addictol/Include/DearModdingUI/Status.h"
#include "../Addictol/Include/DearModdingUI/CarrierMenu.h"
#include "../Addictol/Include/DearModdingUI/FontCatalog.h"
#include "../Addictol/Include/DearModdingUI/HostSettings.h"
#include "../Addictol/Include/DearModdingUI/HostSettingsView.h"
#include "../Addictol/Include/DearModdingUI/IconGlyphs.h"
#include "../Addictol/Include/DearModdingUI/SettingsActions.h"
#include "../Addictol/Include/DearModdingUI/ThemeDefaults.h"
#include "../Addictol/Include/DearModdingUI/VisualDecisions.h"
#include "Harness.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "../Addictol/Include/DearModdingUI/ImGuiFingerprint.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
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

		[[nodiscard]] DMUI_ActionDescriptor Action(
			const char* a_id,
			const char* a_label,
			const char* a_icon,
			int32_t a_sort,
			CallbackState& a_state) noexcept
		{
			return {
				sizeof(DMUI_ActionDescriptor),
				a_id,
				a_label,
				a_icon,
				nullptr,
				a_sort,
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

		[[nodiscard]] DMUI_ActionHandle AddAction(
			Registry& a_registry,
			DMUI_ClientHandle a_client,
			const char* a_id,
			const char* a_label,
			const char* a_icon,
			int32_t a_sort,
			CallbackState& a_state)
		{
			auto descriptor = Action(a_id, a_label, a_icon, a_sort, a_state);
			DMUI_ActionHandle handle{};
			require(a_registry.RegisterAction(a_client, &descriptor, &handle) ==
					DMUI_RESULT_OK,
				"action registration failed");
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

		runner.test("status severity controls expiry and persistence", [] {
			const auto start = StatusClock::time_point{};
			StatusModel model;
			require(model.Set(
						StatusOwnerKind::kHost,
						"Evil Modding",
						DMUI_STATUS_SEVERITY_INFO,
						"Working",
						start) == DMUI_RESULT_OK,
				"info status was rejected");
			require(model.Snapshot(
						start +
						kTransientStatusLifetime -
						std::chrono::milliseconds{ 1 }).has_value(),
				"info status expired early");
			require(!model.Snapshot(start + kTransientStatusLifetime),
				"info status did not expire");

			require(model.Set(
						StatusOwnerKind::kHost,
						"Evil Modding",
						DMUI_STATUS_SEVERITY_SUCCESS,
						"Saved",
						start) == DMUI_RESULT_OK,
				"success status was rejected");
			require(!model.Snapshot(start + kTransientStatusLifetime),
				"success status did not expire");

			require(model.Set(
						StatusOwnerKind::kHost,
						"Evil Modding",
						DMUI_STATUS_SEVERITY_WARNING,
						"Warning",
						start) == DMUI_RESULT_OK,
				"warning status was rejected");
			require(model.Snapshot(start + std::chrono::hours{ 24 }).has_value(),
				"warning status expired");

			require(model.Set(
						StatusOwnerKind::kHost,
						"Evil Modding",
						DMUI_STATUS_SEVERITY_ERROR,
						"Error",
						start) == DMUI_RESULT_OK,
				"error status was rejected");
			require(model.Snapshot(start + std::chrono::hours{ 24 }).has_value(),
				"error status expired");
		});

		runner.test("newest status supersedes older status regardless of severity", [] {
			const auto start = StatusClock::time_point{};
			StatusModel model;
			require(model.Set(
						StatusOwnerKind::kClient,
						"First",
						DMUI_STATUS_SEVERITY_ERROR,
						"Older error",
						start) == DMUI_RESULT_OK,
				"older status was rejected");
			const auto older = model.Snapshot(start);
			require(older.has_value(), "older status was lost");
			require(model.Set(
						StatusOwnerKind::kClient,
						"Second",
						DMUI_STATUS_SEVERITY_INFO,
						"Newer info",
						start + std::chrono::milliseconds{ 1 }) == DMUI_RESULT_OK,
				"newer status was rejected");
			const auto newer = model.Snapshot(start + std::chrono::milliseconds{ 1 });
			require(
					newer &&
						newer->generation > older->generation &&
						newer->owner == "Second" &&
						newer->message == "Newer info",
					"newest status did not supersede an older persistent status");
		});

		runner.test("persistent status can be dismissed without clearing a replacement", [] {
			const auto start = StatusClock::time_point{};
			StatusModel model;
			require(model.Set(
						StatusOwnerKind::kClient,
						"Client",
						DMUI_STATUS_SEVERITY_ERROR,
						"Persistent",
						start) == DMUI_RESULT_OK,
				"persistent status was rejected");
			const auto persistent = model.Snapshot(start);
			require(persistent && model.Dismiss(persistent->generation),
				"persistent status was not dismissed");
			require(!model.Snapshot(start), "dismissed status remained visible");

			require(model.Set(
						StatusOwnerKind::kClient,
						"Client",
						DMUI_STATUS_SEVERITY_WARNING,
						"Older",
						start) == DMUI_RESULT_OK,
				"older persistent status was rejected");
			const auto older = model.Snapshot(start);
			require(model.Set(
						StatusOwnerKind::kClient,
						"Client",
						DMUI_STATUS_SEVERITY_ERROR,
						"Replacement",
						start) == DMUI_RESULT_OK,
				"replacement status was rejected");
			require(older && !model.Dismiss(older->generation),
				"stale dismissal cleared a replacement");
			require(model.Snapshot(start)->message == "Replacement",
				"replacement was lost after stale dismissal");
		});

		runner.test("status attribution distinguishes host and client owners", [] {
			const auto start = StatusClock::time_point{};
			StatusModel model;
			require(model.Set(
						StatusOwnerKind::kHost,
						"Evil Modding",
						DMUI_STATUS_SEVERITY_SUCCESS,
						"Saved",
						start) == DMUI_RESULT_OK,
				"host status was rejected");
			const auto host = model.Snapshot(start);
			require(
					host &&
						host->ownerKind == StatusOwnerKind::kHost &&
						host->attributedText == "Evil Modding: Saved",
					"host status attribution changed");
			require(model.Set(
						StatusOwnerKind::kClient,
						"Community Shaders",
						DMUI_STATUS_SEVERITY_ERROR,
						"Failed",
						start) == DMUI_RESULT_OK,
				"client status was rejected");
			const auto client = model.Snapshot(start);
			require(
					client &&
						client->ownerKind == StatusOwnerKind::kClient &&
						client->attributedText == "Community Shaders: Failed",
					"client status attribution changed");
		});

		runner.test("status truncation preserves full tooltip text", [] {
			const auto measure = [](std::string_view a_text) {
				return static_cast<float>(a_text.size());
			};
			const auto full = std::string{ "Community Shaders: A detailed failure message" };
			const auto truncated = FitStatusText(full, 24.0f, measure);
			require(
					truncated.truncated &&
						truncated.full == full &&
						truncated.visible != full &&
						truncated.visible.ends_with("\xE2\x80\xA6") &&
						measure(truncated.visible) <= 24.0f,
					"truncated status lost its full tooltip text");
			const auto fitting = FitStatusText(full, measure(full), measure);
			require(
					!fitting.truncated &&
						fitting.visible == full &&
						fitting.full == full,
					"fitting status was truncated");
		});

		runner.test("status validation rejects invalid clients and messages", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(
				registry, "status.mod", "Status Mod", fingerprint, state);
			std::string owner;
			require(
					ValidateStatusRequest(
						registry.CopyClientDisplayName(9999, owner),
						DMUI_STATUS_SEVERITY_INFO,
						"Message") == DMUI_RESULT_CLIENT_NOT_FOUND,
					"unaccepted status client was not rejected");
			const auto accepted = registry.CopyClientDisplayName(client, owner);
			require(
					accepted == DMUI_RESULT_OK && owner == "Status Mod",
					"accepted status owner was not copied");
			require(
					ValidateStatusRequest(
						accepted,
						DMUI_STATUS_SEVERITY_INFO,
						nullptr) == DMUI_RESULT_INVALID_ARGUMENT,
					"null status message was not rejected");
			require(
					ValidateStatusRequest(
						accepted,
						DMUI_STATUS_SEVERITY_INFO,
						"") == DMUI_RESULT_INVALID_ARGUMENT,
					"empty status message was not rejected");
			require(
					ValidateStatusRequest(
						accepted,
						99,
						"Message") == DMUI_RESULT_INVALID_ARGUMENT,
					"unknown status severity was not rejected");
		});

		runner.test("wide footer places measured status after metadata", [] {
			const auto layout = ResolveFooterStatusLayout(
				20.0f,
				1200.0f,
				32.0f,
				300.0f,
				180.0f,
				28.0f,
				8.0f,
				36.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				false);
			require(
					layout.statusMinX == 308.0f &&
						layout.statusMaxX == 488.0f,
					"status text did not follow the metadata");
			require(
					layout.metadataMinX == 20.0f &&
						layout.metadataMaxX == 1160.0f &&
						layout.settingsMinX == 1168.0f &&
						layout.footerHeight == 61.0f,
					"wide footer regions were not separated");
		});

		runner.test("narrow footer clamps status without overlap", [] {
			const auto layout = ResolveFooterStatusLayout(
				20.0f,
				320.0f,
				40.0f,
				180.0f,
				300.0f,
				28.0f,
				8.0f,
				40.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				true);
			require(
					layout.metadataMaxX == 272.0f &&
						layout.statusMinX == 188.0f &&
						layout.statusMaxX == 236.0f &&
						layout.dismissMinX == 244.0f &&
						layout.dismissMaxX == 272.0f,
					"narrow status did not fill its clamped span");
			require(
					layout.statusMaxX + 8.0f == layout.dismissMinX &&
						layout.dismissMaxX < layout.settingsMinX,
					"narrow status overlapped footer controls");
		});

		runner.test("persistent footer status reserves dismiss control", [] {
			const auto layout = ResolveFooterStatusLayout(
				0.0f,
				600.0f,
				40.0f,
				200.0f,
				160.0f,
				28.0f,
				8.0f,
				40.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				true);
			require(
					layout.statusMinX == 208.0f &&
						layout.statusMaxX == 368.0f &&
						layout.dismissMinX == 524.0f &&
						layout.dismissMaxX == 552.0f,
					"persistent status geometry changed");
			require(
					layout.statusMaxX + 8.0f <= layout.dismissMinX &&
						layout.dismissMaxX + 8.0f == layout.settingsMinX,
					"persistent footer controls overlapped");
		});

		runner.test("footer metadata geometry stays fixed while idle", [] {
			const auto idle = ResolveFooterStatusLayout(
				20.0f,
				1200.0f,
				32.0f,
				300.0f,
				0.0f,
				28.0f,
				8.0f,
				36.0f,
				8.0f,
				8.0f,
				1.0f,
				false,
				false);
			const auto active = ResolveFooterStatusLayout(
				20.0f,
				1200.0f,
				32.0f,
				300.0f,
				180.0f,
				28.0f,
				8.0f,
				36.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				false);
			require(
					idle.metadataMaxX == active.metadataMaxX &&
						idle.metadataMinX == active.metadataMinX &&
						idle.statusMinX == active.statusMinX,
					"status presence changed reserved footer geometry");
			require(
					idle.metadataMinX == 20.0f &&
						idle.metadataMaxX == 1160.0f &&
						idle.statusMinX == 308.0f,
					"idle footer changed metadata geometry");
		});

		runner.test("footer metadata geometry ignores status length", [] {
			const auto shortStatus = ResolveFooterStatusLayout(
				20.0f,
				1200.0f,
				32.0f,
				300.0f,
				80.0f,
				28.0f,
				8.0f,
				36.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				false);
			const auto longStatus = ResolveFooterStatusLayout(
				20.0f,
				1200.0f,
				32.0f,
				300.0f,
				800.0f,
				28.0f,
				8.0f,
				36.0f,
				8.0f,
				8.0f,
				1.0f,
				true,
				false);
			require(
					shortStatus.metadataMaxX == longStatus.metadataMaxX &&
						shortStatus.metadataMinX ==
							longStatus.metadataMinX &&
						shortStatus.statusMinX == longStatus.statusMinX,
					"status message length changed reserved footer geometry");
			require(
					shortStatus.statusMinX == 308.0f &&
						shortStatus.statusMaxX == 388.0f &&
						longStatus.statusMinX == 308.0f &&
						longStatus.statusMaxX == 1108.0f,
					"status text did not fit after the metadata");
		});

		runner.test("header title aligns with the footer bullet run", [] {
			constexpr float framePaddingX{ 8.0f };
			constexpr float fontSize{ 21.0f };
			const auto inset = BulletRunContentInset(framePaddingX, fontSize);
			const auto bulletInkMinX =
				framePaddingX + fontSize * 0.5f - fontSize * 0.2f;
			require(
					std::abs(inset - bulletInkMinX) < 0.001f,
					"header title inset did not match the bullet ink edge");
			require(
					BulletRunContentInset(-4.0f, -8.0f) == 0.0f,
					"header title inset accepted negative metrics");
		});

		runner.test("row content centers vertically without negative offset", [] {
			require(
					CenterOffsetY(40.0f, 20.0f) == 10.0f &&
						CenterOffsetY(20.0f, 40.0f) == 0.0f &&
						CenterOffsetY(-10.0f, 20.0f) == 0.0f,
					"row content did not center vertically");
		});

		runner.test("capital ink provides stable optical text offset", [] {
			constexpr std::string_view versionLabel{ "Version: 1.6" };
			constexpr std::string_view modLabel{ "Mod: Addictol" };
			constexpr float rowHeight{ 40.0f };
			constexpr float fontSize{ 20.0f };
			constexpr float ascent{ 16.0f };
			constexpr float referenceMinY{ 2.0f };
			constexpr float referenceMaxY{ 16.0f };
			const auto versionOffset =
				OpticalTextOffsetY(
					rowHeight,
					fontSize,
					referenceMinY,
					referenceMaxY,
					1.0f);
			const auto modOffset =
				OpticalTextOffsetY(
					rowHeight,
					fontSize,
					referenceMinY,
					referenceMaxY,
					1.0f);
			const auto boxOffset = CenterOffsetY(rowHeight, fontSize);
			const auto ascentBoxOffset =
				rowHeight * 0.5f - ascent * 0.5f;
			require(
					versionLabel != modLabel &&
						versionOffset == 11.0f &&
						modOffset == versionOffset &&
						versionOffset > boxOffset &&
						versionOffset < ascentBoxOffset,
					"capital ink did not center between box heuristics");
			require(
					OpticalTextOffsetY(
						rowHeight,
						fontSize,
						0.0f,
						0.0f,
						1.0f) == boxOffset &&
						OpticalTextOffsetY(
							rowHeight,
							fontSize,
							referenceMinY,
							referenceMaxY,
							0.0f) == boxOffset,
					"missing reference font data did not use box centering");
		});

		runner.test("footer band keeps symmetric row padding", [] {
			constexpr float verticalSpacing{ 8.0f };
			constexpr float windowPadding{ 10.0f };
			constexpr float separatorThickness{ 3.0f };
			constexpr float rowHeight{ 40.0f };
			const auto adjustment = FooterRowAdjustmentY(
				verticalSpacing,
				windowPadding);
			const auto separatorBottom =
				verticalSpacing * 2.0f + separatorThickness;
			const auto rowTop =
				verticalSpacing * 3.0f +
				separatorThickness +
				adjustment;
			const auto footerHeight = ReservedFooterHeight(
				rowHeight,
				verticalSpacing,
				windowPadding,
				separatorThickness);
			require(
					rowTop - separatorBottom == windowPadding &&
						footerHeight == rowTop + rowHeight,
					"footer row padding was asymmetric");
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
			page.kind = 3;
			require(registry.RegisterPage(handle, &page, &pageHandle) ==
					DMUI_RESULT_INVALID_PAGE_KIND,
				"removed page kind was accepted");
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

		runner.test("action registration validates descriptors clients duplicates and freeze", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto first = AddClient(
				registry, "actions.first", "First", fingerprint, state);
			const auto second = AddClient(
				registry, "actions.second", "Second", fingerprint, state);
			auto action = Action(
				"copy-diagnostics", "Copy diagnostics", "clipboard-text", 0, state);
			DMUI_ActionHandle handle{};

			require(registry.RegisterAction(first, nullptr, &handle) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null action descriptor was accepted");
			require(registry.RegisterAction(first, &action, nullptr) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"null action output was accepted");
			require(registry.RegisterAction(
						DMUI_INVALID_CLIENT_HANDLE, &action, &handle) ==
					DMUI_RESULT_INVALID_ARGUMENT,
				"invalid action client handle was accepted");
			action.structSize = sizeof(action) - 1;
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_STRUCT_TOO_SMALL,
				"short action descriptor was accepted");
			action.structSize = sizeof(action);
			action.callback = nullptr;
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"null action callback was accepted");
			action.callback = &Draw;
			action.id = "invalid action";
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"invalid action ID was accepted");
			action.id = "copy-diagnostics";
			action.displayLabel = "";
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_INVALID_DESCRIPTOR,
				"empty action label was accepted");
			action.displayLabel = "Copy diagnostics";
			require(registry.RegisterAction(9999, &action, &handle) ==
					DMUI_RESULT_CLIENT_NOT_FOUND,
				"action for an unknown client was accepted");
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_OK,
				"valid action was rejected");
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_DUPLICATE_ACTION_ID,
				"duplicate action ID in one client was accepted");
			require(registry.RegisterAction(second, &action, &handle) ==
					DMUI_RESULT_OK,
				"same action ID in another client was rejected");
			require(registry.Freeze(), "action registry did not freeze");
			action.id = "late";
			require(registry.RegisterAction(first, &action, &handle) ==
					DMUI_RESULT_REGISTRATION_CLOSED,
				"action registration remained open after freeze");
		});

		runner.test("client actions order by sort key then stable ID", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto client = AddClient(
				registry, "actions.mod", "Actions", fingerprint, state);
			(void)AddAction(
				registry, client, "zulu", "Zulu", nullptr, 10, state);
			(void)AddAction(
				registry, client, "bravo", "Bravo", nullptr, -10, state);
			(void)AddAction(
				registry, client, "alpha", "Alpha", nullptr, 10, state);
			require(registry.Freeze(), "ordered action registry did not freeze");
			const auto& actions = registry.OrderedActions();
			require(actions.size() == 3, "registered actions were lost");
			require(
					actions[0].id == "bravo" &&
						actions[1].id == "alpha" &&
						actions[2].id == "zulu",
					"actions did not order by sort key then ID");
		});

		runner.test("Addictol home sorts first through ordinary settings ordering", [] {
			const auto fingerprint = Fingerprint();
			Registry registry{ fingerprint };
			CallbackState state;
			const auto addictol = AddClient(
				registry,
				"dear-modding.addictol",
				"Addictol",
				fingerprint,
				state,
				ClientOrigin::kHost);
			const auto communityShaders = AddClient(
				registry,
				"dear-modding.community-shaders",
				"Community Shaders",
				fingerprint,
				state);
			const auto addictolGeneral = AddPage(
				registry,
				addictol,
				"general",
				"General",
				"Addictol",
				10,
				DMUI_PAGE_KIND_SETTINGS,
				state);
			const auto addictolHome = AddPage(
				registry,
				addictol,
				"home",
				"Home",
				"Addictol",
				0,
				DMUI_PAGE_KIND_SETTINGS,
				state);
			(void)AddPage(
				registry,
				communityShaders,
				"home",
				"Home",
				"Community Shaders",
				0,
				DMUI_PAGE_KIND_SETTINGS,
				state);
			require(registry.Freeze(), "ordinary home registry did not freeze");
			const auto& navigation = registry.Navigation();
			const auto* addictolClient = navigation.FindClient(addictol);
			const auto* communityShadersClient =
				navigation.FindClient(communityShaders);
			const auto countHomes = [](const NavigationClient& a_client) {
				size_t count = 0;
				for (const auto& category : a_client.categories)
					count += std::ranges::count_if(category.pages, [](const auto& a_page) {
						return a_page.displayName == "Home";
					});
				return count;
			};
			require(addictolClient &&
					addictolClient->categories.size() == 1 &&
					addictolClient->categories[0].displayName == "Addictol" &&
					addictolClient->categories[0].pages.size() == 2,
				"Addictol pages were not grouped as ordinary settings pages");
			require(addictolClient->categories[0].pages[0].handle == addictolHome &&
					addictolClient->categories[0].pages[1].handle == addictolGeneral,
				"Addictol home did not sort first by its ordinary sort key");
			require(communityShadersClient &&
					communityShadersClient->categories.size() == 1 &&
					communityShadersClient->categories[0].pages.size() == 1 &&
					communityShadersClient->categories[0].pages[0].displayName == "Home",
				"Community Shaders did not retain exactly one ordinary Home entry");
			require(countHomes(*addictolClient) == 1 &&
					countHomes(*communityShadersClient) == 1,
				"clients did not retain exactly one registered Home entry");
			require(registry.PageCount() == 3,
				"the host added an unregistered page");
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
			char actionId[] = "copy";
			char actionLabel[] = "Copy diagnostics";
			char actionIcon[] = "clipboard-text";
			char actionTooltip[] = "Copy a summary.";
			auto action = Action(actionId, actionLabel, actionIcon, 0, state);
			action.tooltip = actionTooltip;
			DMUI_ActionHandle actionHandle{};
			require(registry.RegisterAction(client, &action, &actionHandle) ==
					DMUI_RESULT_OK,
				"copy action failed");
			actionId[0] = 'x';
			actionLabel[0] = 'X';
			actionIcon[0] = 'x';
			actionTooltip[0] = 'X';
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
			require(
					registry.OrderedActions().front().id == "copy" &&
						registry.OrderedActions().front().displayLabel ==
							"Copy diagnostics" &&
						registry.OrderedActions().front().iconName ==
							"clipboard-text" &&
						registry.OrderedActions().front().tooltip ==
							"Copy a summary.",
					"action descriptor strings were not copied");
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

		runner.test("icon names resolve to deterministic Phosphor glyphs", [] {
			require(PhosphorGlyph::kGear == 0xE270,
				"host settings gear glyph changed");
			require(PhosphorGlyph::kX == 0xE4F6,
				"host close glyph changed");
			require(
				PhosphorGlyph::kArrowCounterClockwise == 0xE038 &&
					PhosphorGlyph::kArrowsClockwise == 0xE094 &&
					PhosphorGlyph::kFloppyDisk == 0xE248,
				"settings action glyph codepoints changed");
			require(SlugifyIconName("Post Process") == "post-process",
				"spaces were not collapsed");
			require(SlugifyIconName("Mixed___CASE Name") == "mixed-case-name",
				"underscores or mixed case changed");
			require(SlugifyIconName("A.B/C-D!") == "abcd",
				"punctuation was not dropped");
			require(SlugifyIconName("").empty() && SlugifyIconName("!@#$").empty(),
				"empty icon names produced a slug");

			require(ResolveIconGlyph(IconKind::kCategory, "Lighting") ==
					PhosphorGlyph::kSun &&
					ResolveIconGlyph(IconKind::kCategory, "PERFORMANCE") ==
					PhosphorGlyph::kGauge &&
					ResolveIconGlyph(IconKind::kCategory, "Post Process") ==
					PhosphorGlyph::kMagicWand &&
					ResolveIconGlyph(IconKind::kCategory, "Post-process") ==
					PhosphorGlyph::kMagicWand,
				"known rendering categories changed glyphs");
			require(ResolveIconGlyph(IconKind::kCategory, "Compatibility") ==
					PhosphorGlyph::kPuzzlePiece &&
					ResolveIconGlyph(IconKind::kCategory, "Dev Tools") ==
					PhosphorGlyph::kTerminalWindow &&
					ResolveIconGlyph(IconKind::kCategory, "Misc") ==
					PhosphorGlyph::kDotsThreeCircle &&
					ResolveIconGlyph(IconKind::kCategory, "Diagnostics") ==
					PhosphorGlyph::kTerminalWindow,
				"known utility categories changed glyphs");
			require(ResolveIconGlyph(IconKind::kCategory, "Unloaded") ==
					PhosphorGlyph::kArchive &&
					ResolveIconGlyph(IconKind::kCategory, "Other") ==
					PhosphorGlyph::kDotsThreeCircle &&
					ResolveIconGlyph(IconKind::kCategory, "Overlay") ==
					PhosphorGlyph::kAppWindow,
				"host categories did not resolve to dedicated glyphs");
			require(
				ResolveIconGlyph(IconKind::kCategory, "Stability") ==
						PhosphorGlyph::kShieldCheck &&
					ResolveIconGlyph(IconKind::kCategory, "Visuals") ==
						PhosphorGlyph::kPalette &&
					ResolveIconGlyph(IconKind::kCategory, "Audio") ==
						PhosphorGlyph::kSpeakerHigh &&
					ResolveIconGlyph(IconKind::kCategory, "Gameplay") ==
						PhosphorGlyph::kGameController &&
					ResolveIconGlyph(IconKind::kCategory, "Interface") ==
						PhosphorGlyph::kMonitor,
				"settings categories did not resolve to dedicated glyphs");
			require(ResolveIconGlyph(
						IconKind::kClient,
						"dear-modding.addictol") ==
					PhosphorGlyph::kPuzzlePiece &&
					ResolveIconGlyph(
						IconKind::kClient,
						"dear-modding.community-shaders") ==
					PhosphorGlyph::kSun,
				"known clients changed glyphs");
			require(ResolveCategoryIconGlyph(
						"Community Shaders",
						"Community Shaders",
						"dearmodding.community-shaders") ==
					PhosphorGlyph::kSun &&
					ResolveCategoryIconGlyph(
						"Addictol",
						"Addictol",
						"dear-modding.addictol") ==
					PhosphorGlyph::kPuzzlePiece,
				"registered client-named categories did not inherit client glyphs");
			require(ResolveCategoryIconGlyph(
						"Dear.Modding-Addictol",
						"Different Name",
						"dear-modding.addictol") ==
					PhosphorGlyph::kPuzzlePiece,
				"client ID matching did not use the client glyph lookup");
			require(ResolveCategoryIconGlyph(
						"COMMUNITY--SHADERS!",
						"Community Shaders",
						"dearmodding.community-shaders") ==
					PhosphorGlyph::kSun,
				"client-named category normalization changed");
			require(ResolveCategoryIconGlyph(
						"Lighting",
						"Community Shaders",
						"dearmodding.community-shaders") ==
					PhosphorGlyph::kSun &&
					ResolveCategoryIconGlyph(
						"Unknown",
						"Community Shaders",
						"dearmodding.community-shaders") ==
					PhosphorGlyph::kQuestion,
				"non-matching categories were affected by client naming");
			require(ResolveIconGlyph(IconKind::kCategory, "Unknown") ==
					PhosphorGlyph::kQuestion &&
					ResolveIconGlyph(IconKind::kClient, "") ==
					PhosphorGlyph::kQuestion,
				"unknown icon names did not use the fallback");
			require(
					ResolveActionIconGlyph("clipboard-text") ==
							PhosphorGlyph::kClipboardText &&
						ResolveActionIconGlyph("Clear Cache") ==
							PhosphorGlyph::kTrash &&
						ResolveActionIconGlyph("arrows-clockwise") ==
							PhosphorGlyph::kArrowsClockwise &&
						ResolveActionIconGlyph("floppy_disk") ==
							PhosphorGlyph::kFloppyDisk &&
						ResolveActionIconGlyph("restore_settings") ==
							PhosphorGlyph::kArrowCounterClockwise,
					"known action icon names changed glyphs");
			require(ResolveActionIconGlyph("unknown") == char32_t{},
				"unknown action icon name did not request text fallback");
		});

		runner.test("settings actions map to deterministic glyphs", [] {
			require(
				kSettingsActionOrder ==
					std::array{
						SettingsAction::kReset,
						SettingsAction::kRevert,
						SettingsAction::kApply },
				"settings action order changed");
			require(
				SettingsActionGlyph(SettingsAction::kReset) ==
						PhosphorGlyph::kArrowsClockwise &&
					SettingsActionGlyph(SettingsAction::kRevert) ==
						PhosphorGlyph::kArrowCounterClockwise &&
					SettingsActionGlyph(SettingsAction::kApply) ==
						PhosphorGlyph::kFloppyDisk,
				"settings actions changed glyphs");
		});

		runner.test("settings action buttons fall back when font glyphs are absent", [] {
			for (const auto action : kSettingsActionOrder)
			{
				const auto glyph = SettingsActionGlyph(action);
				const auto icon =
					ResolveSettingsActionButtonPresentation(action, true);
				const auto fallback =
					ResolveSettingsActionButtonPresentation(action, false);
				require(
					icon.glyph == glyph &&
						!icon.useTextFallback &&
						fallback.glyph == char32_t{} &&
						fallback.useTextFallback,
					"missing settings glyph did not select text fallback");
			}
		});

		runner.test("theme icon tint selects colored and monochrome modes", [] {
			const HostAccentColor storedAccent{ 0x00, 0x72, 0xB2 };
			const auto accent = HostAccentToImVec4(storedAccent);
			const ImVec4 text{ 1.0f, 1.0f, 1.0f, 1.0f };
			require(Theme::kIconDefaults.colorMode == Theme::IconColorMode::kColored,
				"default icon mode is not colored");
			require(SameColor(
						Theme::ResolveIconTint(
							Theme::IconColorMode::kColored,
							accent,
							text),
						accent),
				"colored icons did not use the accent tint");
			require(SameColor(
						Theme::ResolveIconTint(
							Theme::IconColorMode::kMonochrome,
							accent,
							text),
						text),
				"monochrome icons did not use the text tint");
			require(SameColor(
						Theme::ResolveIconTint(
							Theme::IconColorMode::kColored,
							accent,
							text),
						accent) &&
					SameColor(
						Theme::ResolveIconTint(
							Theme::IconColorMode::kMonochrome,
							accent,
							text),
						text),
				"persisted icon mode did not select its runtime tint");
			require(
				DecodeHostAccentColor(EncodeHostAccentColor(storedAccent)) ==
					storedAccent,
				"accent color did not preserve icon tint bytes");
		});

		runner.test("host breadcrumb identifies zero or one selected client", [] {
			require(BuildHostBreadcrumb("Evil Modding", "") == "Evil Modding",
				"empty selection changed the host-only breadcrumb");
			require(
					BuildHostBreadcrumb("Evil Modding", "Community Shaders") ==
						"Evil Modding > Community Shaders",
					"selected client was not added to the breadcrumb");
			require(
					BuildHostBreadcrumb("Evil Modding", "Interface Settings") ==
						"Evil Modding > Interface Settings",
					"settings view was not identified in the breadcrumb");
			require(ShouldDrawHeaderClose(false, true),
				"undocked titleless host lost its close button");
			require(
					!ShouldDrawHeaderClose(true, true) &&
						!ShouldDrawHeaderClose(true, false) &&
						!ShouldDrawHeaderClose(false, false),
					"host close duplicated a native or docked close affordance");
		});

		runner.test("host close and footer gear stay clear of adjacent content", [] {
			struct Case
			{
				float fontSize;
				float uiScale;
			};
			constexpr std::array cases{
				Case{ 16.0f, 1.0f },
				Case{ 18.0f, 1.25f },
				Case{ 21.0f, 1.5f },
				Case{ 28.0f, 2.0f }
			};
			for (const auto& test : cases)
			{
				const auto fontSize = test.fontSize * test.uiScale;
				const auto padding = 2.0f * test.uiScale;
				const auto spacing = 8.0f * test.uiScale;
				const auto iconSize = HostChromeIconSize(fontSize);
				const auto extent = HostChromeButtonExtent(fontSize, padding);
				const auto header = ResolveTrailingControlLayout(
					24.0f, 1896.0f, extent, spacing);
				const auto footer = ResolveTrailingControlLayout(
					36.0f, 1264.0f, extent, spacing);
				require(
						iconSize == fontSize * 1.5f &&
							extent > TitleBarButtonExtent(fontSize, padding),
						"host chrome did not use its larger icon scale");
				require(
						header.controlMaxX == 1896.0f &&
							header.controlMinX == 1896.0f - extent &&
							header.adjacentMaxX ==
								header.controlMinX - spacing,
						"close button geometry changed");
				require(
						footer.controlMaxX == 1264.0f &&
							footer.controlMinX == 1264.0f - extent &&
							footer.adjacentMaxX ==
								footer.controlMinX - spacing,
						"footer gear geometry changed");
				require(
						header.adjacentMaxX <= header.controlMinX &&
							footer.adjacentMaxX <= footer.controlMinX,
						"host chrome overlapped adjacent content");
			}
		});

		runner.test("page action row reserves space only for registered actions", [] {
			const auto empty = ResolvePageActionRowLayout(
				100.0f, 900.0f, 0.0f, 0, 8.0f);
			require(
					empty.titleMaxX == 900.0f &&
						empty.actionsMinX == 900.0f &&
						empty.actionsMaxX == 900.0f &&
						empty.reservedWidth == 0.0f,
					"client with no actions reserved title-row space");

			const auto populated = ResolvePageActionRowLayout(
				100.0f, 900.0f, 72.0f, 2, 8.0f);
			require(
					populated.actionsMinX == 820.0f &&
						populated.titleMaxX == 812.0f &&
						populated.reservedWidth == 80.0f,
					"registered actions did not reserve their exact strip");
			require(populated.titleMaxX <= populated.actionsMinX,
				"page title overlapped client actions");
		});

		runner.test("settings action rows keep fixed non-overlapping geometry", [] {
			struct Case
			{
				float fontSize;
				float uiScale;
			};
			constexpr std::array cases{
				Case{ 16.0f, 1.0f },
				Case{ 18.0f, 1.25f },
				Case{ 21.0f, 1.5f },
				Case{ 28.0f, 2.0f }
			};
			for (const auto& test : cases)
			{
				const auto fontSize = test.fontSize * test.uiScale;
				const auto buttonPadding = 2.0f * test.uiScale;
				const auto framePadding = 8.0f * test.uiScale;
				const auto spacing = 4.0f * test.uiScale;
				const auto buttonExtent = TitleBarButtonExtent(
					fontSize, buttonPadding);
				const std::array widths{
					ActionButtonWidth(true, 0.0f, buttonExtent, framePadding),
					ActionButtonWidth(true, 0.0f, buttonExtent, framePadding),
					ActionButtonWidth(true, 0.0f, buttonExtent, framePadding)
				};
				const auto cleanWidthSum =
					ResolveSettingsActionButtonWidthSum(widths, false, 0);
				const auto dirtyWidthSum =
					ResolveSettingsActionButtonWidthSum(widths, true, 7);
				require(cleanWidthSum == dirtyWidthSum,
					"draft state or pending count changed action-row extent");
				const auto clean = ResolveHostSettingsTitleRowLayout(
					100.0f,
					1900.0f,
					cleanWidthSum,
					widths.size(),
					buttonExtent,
					spacing);
				const auto dirty = ResolveHostSettingsTitleRowLayout(
					100.0f,
					1900.0f,
					dirtyWidthSum,
					widths.size(),
					buttonExtent,
					spacing);
				const auto cleanPage = ResolvePageActionRowLayout(
					100.0f,
					1900.0f,
					cleanWidthSum,
					widths.size(),
					spacing);
				const auto dirtyPage = ResolvePageActionRowLayout(
					100.0f,
					1900.0f,
					dirtyWidthSum,
					widths.size(),
					spacing);
				const auto priorClose = ResolveTrailingControlLayout(
					100.0f, 1900.0f, buttonExtent, spacing);
				require(
					clean.titleMaxX <= clean.actionsMinX &&
						clean.actionsMaxX + spacing == clean.closeMinX &&
						clean.closeMinX == priorClose.controlMinX &&
						clean.closeMaxX == priorClose.controlMaxX,
					"settings title controls overlapped");

				auto position = clean.actionsMinX;
				for (const auto width : widths)
				{
					require(position + width <= clean.actionsMaxX,
						"settings action exceeded its reserved strip");
					position += width + spacing;
				}
				require(position - spacing == clean.actionsMaxX,
					"settings action spacing changed");
				require(
					clean.reservedWidth == dirty.reservedWidth &&
						clean.actionsMinX == dirty.actionsMinX &&
						clean.closeMinX == dirty.closeMinX,
					"dirty state changed settings title geometry");
				require(
					cleanPage.reservedWidth == dirtyPage.reservedWidth &&
						cleanPage.actionsMinX == dirtyPage.actionsMinX,
					"pending count changed Addictol settings action geometry");
			}
		});

		runner.test("settings action availability follows dirty state", [] {
			require(
				!SettingsActionEnabled(SettingsAction::kApply, false) &&
					!SettingsActionEnabled(SettingsAction::kRevert, false) &&
					SettingsActionEnabled(SettingsAction::kReset, false),
				"clean settings exposed the wrong title actions");

			require(
				SettingsActionEnabled(SettingsAction::kApply, true) &&
					SettingsActionEnabled(SettingsAction::kRevert, true) &&
					SettingsActionEnabled(SettingsAction::kReset, true),
				"dirty settings exposed the wrong title actions");
		});

		runner.test("host settings panel follows menu visibility", [] {
			auto open = DecideHostSettingsPanelOpen(
				false,
				false,
				HostSettingsPanelEvent::kToggleRequested);
			require(!open, "settings opened while the menu was closed");

			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kToggleRequested);
			require(open, "settings did not open from the visible menu");
			require(DecideHostSettingsPanelOpen(
							open,
							true,
							HostSettingsPanelEvent::kNone),
				"settings did not remain open");

			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kDismissed);
			require(!open, "settings did not dismiss");
			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kToggleRequested);
			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kModSelected);
			require(!open, "settings remained open after mod selection");
			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kToggleRequested);
			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kToggleRequested);
			require(!open, "settings gear did not toggle the view off");
			open = DecideHostSettingsPanelOpen(
				open,
				true,
				HostSettingsPanelEvent::kToggleRequested);
			open = DecideHostSettingsPanelOpen(
				open,
				false,
				HostSettingsPanelEvent::kMenuClosed);
			require(!open, "settings remained open after the menu closed");
			require(!DecideHostSettingsPanelOpen(
							open,
							true,
							HostSettingsPanelEvent::kNone),
				"settings reopened with the menu");
		});

		runner.test("host settings draft detects unapplied changes", [] {
			const auto committed = DefaultHostInterfaceSettings();
			auto state = BeginHostSettingsDraft(committed);
			require(state.active, "settings draft did not activate");
			require(!HostSettingsDraftDiffers(state),
				"unchanged settings draft was marked dirty");

			state.draft.accentColor = { 0x00, 0x72, 0xB2 };
			require(HostSettingsDraftDiffers(state),
				"changed settings draft was not marked dirty");
			require(state.committed == committed,
				"editing the draft changed committed settings");
		});

		runner.test("host settings preview excludes typography", [] {
			const auto committed = DefaultHostInterfaceSettings();
			auto draft = committed;
			draft.uiScale = 1.50f;
			draft.bodyFontFamily = "Atkinson Hyperlegible";
			require(PreviewHostInterfaceSettings(draft) ==
					PreviewHostInterfaceSettings(committed),
				"typography settings leaked into the live preview");

			draft.accentColor = { 0x00, 0x72, 0xB2 };
			require(PreviewHostInterfaceSettings(draft) !=
					PreviewHostInterfaceSettings(committed),
				"appearance change was omitted from the live preview");
		});

		runner.test("host settings draft applies all fields once", [] {
			auto state = BeginHostSettingsDraft(
				DefaultHostInterfaceSettings());
			const auto unchanged = ApplyHostSettingsDraft(state);
			require(!unchanged.settings,
				"unchanged settings draft produced a commit");

			const HostInterfaceSettings changed{
				Theme::IconColorMode::kMonochrome,
				{ 0xD5, 0x5E, 0x00 },
				0.80f,
				false,
				0.75f,
				1.75f,
				"Atkinson Hyperlegible"
			};
			state.draft = changed;
			const auto applied = ApplyHostSettingsDraft(state);
			require(applied.settings && *applied.settings == changed,
				"settings draft did not commit every changed field");
			require(state.committed == changed &&
					!HostSettingsDraftDiffers(state),
				"applied settings draft remained dirty");

			const auto repeated = ApplyHostSettingsDraft(state);
			require(!repeated.settings,
				"applied settings draft committed more than once");
		});

		runner.test("host settings draft reverts leaves and resets", [] {
			const HostInterfaceSettings committed{
				Theme::IconColorMode::kMonochrome,
				{ 0x00, 0x72, 0xB2 },
				0.80f,
				false,
				0.75f,
				1.50f,
				"Atkinson Hyperlegible"
			};
			auto state = BeginHostSettingsDraft(committed);
			state.draft = DefaultHostInterfaceSettings();
			RevertHostSettingsDraft(state);
			require(state.draft == committed &&
					!HostSettingsDraftDiffers(state),
				"revert did not restore committed preview fields");

			state.draft.accentColor = { 0xE6, 0x9F, 0x00 };
			LeaveHostSettingsDraft(state);
			require(!state.active && state.draft == committed,
				"leaving settings did not discard the draft");

			state = BeginHostSettingsDraft(committed);
			ResetHostSettingsDraft(state);
			require(state.draft == DefaultHostInterfaceSettings(),
				"reset did not populate shipped defaults");
			require(state.committed == committed &&
					HostSettingsDraftDiffers(state),
				"reset committed instead of updating the draft");
		});

		runner.test("host settings atlas rebuild predicate is exact", [] {
			const auto committed = DefaultHostInterfaceSettings();
			auto draft = committed;
			require(!HostSettingsDraftRequiresAtlasRebuild(
						 committed, draft),
				"unchanged settings requested an atlas rebuild");

			draft.uiScale = 1.25f;
			require(HostSettingsDraftRequiresAtlasRebuild(
						committed, draft),
				"UI scale change did not request an atlas rebuild");

			draft = committed;
			draft.bodyFontFamily = "Atkinson Hyperlegible";
			require(HostSettingsDraftRequiresAtlasRebuild(
						committed, draft),
				"font family change did not request an atlas rebuild");

			draft.uiScale = 1.25f;
			require(HostSettingsDraftRequiresAtlasRebuild(
						committed, draft),
				"combined typography changes did not request an atlas rebuild");
		});

		runner.test("host settings persistence round trips every stored value", [] {
			std::array settings{
				DefaultHostInterfaceSettings(),
				HostInterfaceSettings{
					Theme::IconColorMode::kMonochrome,
					{ 0x00, 0x72, 0xB2 },
					0.80f,
					false,
					0.75f,
					1.75f,
					"Atkinson Hyperlegible"
				},
				HostInterfaceSettings{
					Theme::IconColorMode::kColored,
					{ 0xD5, 0x5E, 0x00 },
					kMinWindowBackgroundOpacity,
					true,
					kMinBackgroundBlurStrength,
					Theme::kMinUserScale,
					"Jost"
				}
			};
			for (const auto& runtime : settings)
			{
				const auto persisted = EncodeHostInterfaceSettings(runtime);
				require(
					DecodeHostInterfaceSettings(persisted) == runtime,
					"stored host settings did not round trip");
				require(
					EncodeHostInterfaceSettings(
						DecodeHostInterfaceSettings(persisted)) == persisted,
					"encoded host settings did not round trip");
			}
		});

		runner.test("host settings clamp malformed persisted values and reset", [] {
			PersistedHostInterfaceSettings persisted;
			persisted.accentColor = "#GG00ZZ";
			persisted.windowBackgroundOpacity = -5.0f;
			persisted.backgroundBlurStrength =
				std::numeric_limits<float>::infinity();
			persisted.uiScale = 99.0f;
			persisted.bodyFontFamily = "..\\escaped";
			const auto decoded = DecodeHostInterfaceSettings(persisted);
			require(
				decoded.accentColor == kDefaultHostAccentColor,
				"malformed accent did not fall back");
			require(
				decoded.windowBackgroundOpacity ==
					kMinWindowBackgroundOpacity,
				"window opacity did not clamp");
			require(
				decoded.backgroundBlurStrength ==
					kDefaultBackgroundBlurStrength,
				"non-finite blur strength did not fall back");
			require(
				decoded.uiScale == Theme::kMaxUserScale,
				"UI scale did not clamp");
			require(
				decoded.bodyFontFamily == kDefaultBodyFontFamily,
				"malformed font family did not fall back");
			require(
				DefaultHostInterfaceSettings() == HostInterfaceSettings{},
				"reset did not restore shipped defaults");
		});

		runner.test("font families enumerate regular faces and fall back", [] {
			struct RemoveTree
			{
				std::filesystem::path path;
				~RemoveTree()
				{
					std::error_code error;
					std::filesystem::remove_all(path, error);
				}
			};

			const auto root =
				std::filesystem::temp_directory_path() /
				"AddictolDearModdingUIFontCatalog";
			std::error_code error;
			std::filesystem::remove_all(root, error);
			const RemoveTree cleanup{ root };
			std::filesystem::create_directories(root / "Jost", error);
			std::filesystem::create_directories(
				root / "Atkinson Hyperlegible", error);
			std::filesystem::create_directories(root / "Phosphor", error);
			std::ofstream(root / "Jost" / "Jost-Regular.ttf").put('\0');
			std::ofstream(
				root /
				"Atkinson Hyperlegible" /
				"AtkinsonHyperlegible-Bold.ttf").put('\0');
			std::ofstream(
				root /
				"Atkinson Hyperlegible" /
				"AtkinsonHyperlegible-Regular.ttf").put('\0');
			std::ofstream(
				root /
				"Phosphor" /
				"Phosphor-Fill.ttf").put('\0');

			const auto families = FontCatalog::Enumerate(root);
			require(families.size() == 2,
				"font family folders were not enumerated safely");
			const auto* atkinson = FontCatalog::Resolve(
				"atkinson hyperlegible", families, "Jost");
			require(
				atkinson &&
					atkinson->name == "Atkinson Hyperlegible" &&
					atkinson->regularFile.ends_with(
						"AtkinsonHyperlegible-Regular.ttf"),
				"font family did not choose its regular face");
			const auto* fallback = FontCatalog::Resolve(
				"Missing Family", families, "Jost");
			require(fallback && fallback->name == "Jost",
				"missing font family did not fall back to Jost");
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

		runner.test("theme style scalars are independently pinned", [] {
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

		runner.test("theme full palette is independently pinned", [] {
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
			const ImVec4 customAccent{ 0.2f, 0.4f, 0.8f, 1.0f };
			const auto customized =
				Theme::MakeEffectivePalette(customAccent, 0.85f);
			require(
				customized[ImGuiCol_Button].x == customAccent.x &&
					customized[ImGuiCol_Button].y == customAccent.y &&
					customized[ImGuiCol_Button].z == customAccent.z &&
					customized[ImGuiCol_WindowBg].w == 0.85f,
				"accent or window opacity did not drive the effective palette");
		});

		runner.test("theme font roles and scaling stay exact", [] {
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
			require(ResolveUiScale(2.0f, 1080) == 1.0f, "DPI altered theme scaling");
			require(ResolveUiScale(1.0f, 2160) == 2.0f, "4K UI scale changed");
			require(
				Theme::ResolveRoleFontSize(
					Theme::FontRole::kBody,
					1080,
					Theme::kMaxUserScale) == 42.0f &&
					ResolveUiScale(
						1.0f,
						1080,
						Theme::kMaxUserScale) == 2.0f,
				"accessibility UI scale was not applied after resolution scaling");
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

		runner.test("glyph origin centers asymmetric ink bounds", [] {
			const auto origin = ResolveCenteredGlyphOrigin(
				100.0f,
				80.0f,
				2.0f,
				4.0f,
				14.0f,
				18.0f,
				1.5f);
			require(
				origin.x == 88.0f &&
					origin.y == 63.5f &&
					origin.x + (2.0f + 14.0f) * 1.5f * 0.5f ==
						100.0f &&
					origin.y + (4.0f + 18.0f) * 1.5f * 0.5f ==
						80.0f,
				"glyph ink bounds were not centered");
		});

		runner.test("cursor ownership follows modal visibility", [] {
			const auto overlay = DecideCursorPresentation(false);
			require(!overlay.captureInput &&
					!overlay.hideOperatingSystemCursor &&
					!overlay.drawSoftwareCursor &&
					!overlay.drawCustomCursor,
				"overlay-only drawing acquired a cursor");

			const auto modal = DecideCursorPresentation(true);
			require(modal.captureInput &&
					modal.hideOperatingSystemCursor &&
					modal.drawSoftwareCursor &&
					!modal.drawCustomCursor,
				"modal drawing did not own exactly one software cursor");
			require(
				static_cast<uint32_t>(modal.drawSoftwareCursor) +
						static_cast<uint32_t>(modal.drawCustomCursor) ==
					1,
				"the modal host did not present exactly one cursor");

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

		runner.test("carrier menu open and close messages remain balanced", [] {
			CarrierMenu::State state{};
			require(
				CarrierMenu::Transition(state, CarrierMenu::Event::kOpen) ==
					CarrierMenu::Action::kShow,
				"the first modal open did not show the carrier");
			require(
				CarrierMenu::Transition(state, CarrierMenu::Event::kOpen) ==
					CarrierMenu::Action::kNone,
				"a repeated modal frame showed the carrier twice");
			require(
				CarrierMenu::Transition(state, CarrierMenu::Event::kClose) ==
					CarrierMenu::Action::kHide,
				"the modal close did not hide the carrier");
			require(
				CarrierMenu::Transition(state, CarrierMenu::Event::kClose) ==
					CarrierMenu::Action::kNone,
				"a repeated close hid the carrier twice");
			require(!state.open, "the balanced sequence retained cursor ownership");
		});

		runner.test("carrier menu cleanup paths hide exactly one open entry", [] {
			constexpr std::array cleanupEvents{
				CarrierMenu::Event::kShutdown,
				CarrierMenu::Event::kBackendFailure,
				CarrierMenu::Event::kRetarget,
				CarrierMenu::Event::kGameTransition,
				CarrierMenu::Event::kOverlayOnly
			};
			for (const auto event : cleanupEvents)
			{
				CarrierMenu::State state{};
				require(
					CarrierMenu::Transition(state, CarrierMenu::Event::kOpen) ==
						CarrierMenu::Action::kShow,
					"a cleanup scenario did not establish an open carrier");
				require(
					CarrierMenu::Transition(state, event) ==
						CarrierMenu::Action::kHide,
					"a cleanup scenario did not balance its show");
				require(
					CarrierMenu::Transition(state, event) ==
						CarrierMenu::Action::kNone,
					"a cleanup scenario queued a second hide");
				require(!state.open, "a cleanup scenario retained cursor ownership");
			}

			CarrierMenu::State overlay{};
			require(
				CarrierMenu::Transition(
					overlay,
					CarrierMenu::Event::kOverlayOnly) ==
					CarrierMenu::Action::kNone,
				"an overlay-only frame opened or hid a carrier");
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

			auto drawActionDescriptor = Action(
				"throw", "Throw", nullptr, 0, drawState);
			drawActionDescriptor.callback = &ThrowDraw;
			DMUI_ActionHandle drawAction{};
			require(drawRegistry.RegisterAction(
						drawClient, &drawActionDescriptor, &drawAction) ==
					DMUI_RESULT_OK,
				"throwing action was not registered");
			require(drawRegistry.InvokeAction(drawAction) ==
					DMUI_RESULT_CALLBACK_FAILED,
				"a throwing action escaped its host guard");
			require(drawRegistry.ActionFailed(drawAction),
				"faulted action was not permanently disabled");
			require(drawRegistry.InvokeAction(drawAction) ==
					DMUI_RESULT_CALLBACK_FAILED,
				"a faulted action was invoked again");

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
			const auto action = AddAction(
				registry, client, "draw", "Draw", nullptr, 0, state);
			require(registry.InvokeAction(action) == DMUI_RESULT_OK,
				"action callback failed");
			require(state.draws == 2, "action callback did not receive userdata");
			require(registry.InvokeAction(action + 1) == DMUI_RESULT_ACTION_NOT_FOUND,
				"unknown action was invoked");
		});
	}
}
