#pragma once

#include <DearModdingUI/API.h>
#include <DearModdingUI/Navigation.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol::DearModdingUI
{
	enum class ClientOrigin : uint32_t
	{
		kHost,
		kExternal
	};

	struct RegisteredClient
	{
		DMUI_ClientHandle handle{ DMUI_INVALID_CLIENT_HANDLE };
		ClientOrigin origin{ ClientOrigin::kExternal };
		std::string id;
		std::string displayName;
		uint32_t version{ 0 };
		DMUI_ClientCapabilities capabilities{ DMUI_CLIENT_CAPABILITY_NONE };
		DMUI_HostReadyCallback onHostReady{ nullptr };
		DMUI_HostUnavailableCallback onHostUnavailable{ nullptr };
		void* userData{ nullptr };
		bool notified{ false };
		bool callbackFailed{ false };
	};

	struct RegisteredPage
	{
		DMUI_PageHandle handle{ DMUI_INVALID_PAGE_HANDLE };
		DMUI_ClientHandle client{ DMUI_INVALID_CLIENT_HANDLE };
		std::string clientId;
		std::string clientDisplayName;
		ClientOrigin clientOrigin{ ClientOrigin::kExternal };
		std::string id;
		std::string displayName;
		std::string category;
		std::string summary;
		std::string imguiLabel;
		int32_t sortKey{ 0 };
		DMUI_PageKind kind{ 0 };
		DMUI_PageDrawCallback draw{ nullptr };
		void* userData{ nullptr };
		uint32_t frameDemand{ 0 };
		bool callbackFailed{ false };
	};

	struct RegisteredAction
	{
		DMUI_ActionHandle handle{ DMUI_INVALID_ACTION_HANDLE };
		DMUI_ClientHandle client{ DMUI_INVALID_CLIENT_HANDLE };
		std::string id;
		std::string displayLabel;
		std::string iconName;
		std::string tooltip;
		int32_t sortKey{ 0 };
		DMUI_ActionCallback callback{ nullptr };
		void* userData{ nullptr };
		bool callbackFailed{ false };
	};

	class Registry
	{
	public:
		explicit Registry(DMUI_ImGuiFingerprint a_fingerprint);

		[[nodiscard]] DMUI_Result RegisterClient(
			const DMUI_ClientDescriptor* a_descriptor,
			DMUI_ClientHandle* a_client,
			ClientOrigin a_origin) noexcept;
		[[nodiscard]] DMUI_Result RegisterPage(
			DMUI_ClientHandle a_client,
			const DMUI_PageDescriptor* a_descriptor,
			DMUI_PageHandle* a_page) noexcept;
		[[nodiscard]] DMUI_Result RegisterAction(
			DMUI_ClientHandle a_client,
			const DMUI_ActionDescriptor* a_descriptor,
			DMUI_ActionHandle* a_action) noexcept;
		[[nodiscard]] bool Freeze() noexcept;
		[[nodiscard]] bool IsOpen() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] size_t ClientCount() const noexcept;
		[[nodiscard]] size_t PageCount() const noexcept;
		[[nodiscard]] size_t DemandedOverlayCount() const noexcept;
		[[nodiscard]] bool HasSettingsPages() const noexcept;
		[[nodiscard]] const std::vector<RegisteredPage>& OrderedPages() const noexcept;
		[[nodiscard]] const std::vector<RegisteredAction>& OrderedActions() const noexcept;
		[[nodiscard]] const NavigationModel& Navigation() const noexcept;
		[[nodiscard]] DMUI_Result RequestFrame(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept;
		[[nodiscard]] DMUI_Result ReleaseFrame(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) noexcept;
		[[nodiscard]] bool IsFrameDemanded(DMUI_PageHandle a_page) const noexcept;
		[[nodiscard]] DMUI_Result ValidatePage(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page,
			DMUI_PageKind a_kind) const noexcept;
		[[nodiscard]] DMUI_Result ValidateSwapChainClient(
			DMUI_ClientHandle a_client) const noexcept;
		[[nodiscard]] DMUI_Result CopyClientDisplayName(
			DMUI_ClientHandle a_client,
			std::string& a_displayName) const noexcept;
		[[nodiscard]] DMUI_Result InvokePage(DMUI_PageHandle a_page) noexcept;
		[[nodiscard]] bool PageFailed(DMUI_PageHandle a_page) const noexcept;
		void MarkPageFailed(DMUI_PageHandle a_page) noexcept;
		[[nodiscard]] DMUI_Result InvokeAction(DMUI_ActionHandle a_action) noexcept;
		[[nodiscard]] bool ActionFailed(DMUI_ActionHandle a_action) const noexcept;
		void MarkActionFailed(DMUI_ActionHandle a_action) noexcept;
		void NotifyReady(const DMUI_HostReadyInfo& a_info) noexcept;
		void NotifyUnavailable(DMUI_UnavailableReason a_reason) noexcept;

		[[nodiscard]] const DMUI_ImGuiFingerprint& Fingerprint() const noexcept;
		[[nodiscard]] static bool SupportsVersion(uint32_t a_requestedVersion) noexcept;
		[[nodiscard]] static bool FingerprintsMatch(
			const DMUI_ImGuiFingerprint& a_left,
			const DMUI_ImGuiFingerprint& a_right) noexcept;

	private:
		enum class Notification : uint32_t
		{
			kNone,
			kReady,
			kUnavailable
		};

		[[nodiscard]] RegisteredClient* FindClient(DMUI_ClientHandle a_client) noexcept;
		[[nodiscard]] const RegisteredClient* FindClient(DMUI_ClientHandle a_client) const noexcept;
		[[nodiscard]] RegisteredPage* FindPage(DMUI_PageHandle a_page) noexcept;
		[[nodiscard]] const RegisteredPage* FindPage(DMUI_PageHandle a_page) const noexcept;
		[[nodiscard]] RegisteredAction* FindAction(DMUI_ActionHandle a_action) noexcept;
		[[nodiscard]] const RegisteredAction* FindAction(DMUI_ActionHandle a_action) const noexcept;
		[[nodiscard]] bool OwnsPage(
			DMUI_ClientHandle a_client,
			DMUI_PageHandle a_page) const noexcept;

		DMUI_ImGuiFingerprint m_fingerprint{};
		mutable std::mutex m_mutex;
		std::vector<RegisteredClient> m_clients;
		std::vector<RegisteredPage> m_pages;
		std::vector<RegisteredAction> m_actions;
		NavigationModel m_navigation;
		DMUI_ClientHandle m_nextClient{ 1 };
		DMUI_PageHandle m_nextPage{ 1 };
		DMUI_ActionHandle m_nextAction{ 1 };
		Notification m_notification{ Notification::kNone };
		bool m_open{ true };
	};
}
