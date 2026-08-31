#pragma once

#include <DearModdingUI/API.h>
#include <DearModdingUI/Registry.h>
#include <DearModdingUI/Status.h>

#include <optional>
#include <string_view>

namespace Addictol::DearModdingUI
{
	inline constexpr std::string_view kHostDisplayName{ "Evil Modding" };

	[[nodiscard]] const DMUI_ImGuiFingerprint& HostFingerprint() noexcept;
	[[nodiscard]] const DMUI_HostAPI& HostAPI() noexcept;

	void Initialize() noexcept;
	void SetBackendUnavailable(DMUI_UnavailableReason a_reason) noexcept;
	void DeferBackendUnavailable(DMUI_UnavailableReason a_reason) noexcept;
	[[nodiscard]] bool BeginBackendInitialization() noexcept;
	void CompleteBackendInitialization(void* a_imguiContext) noexcept;
	void FailBackendInitialization() noexcept;
	[[nodiscard]] bool HasClients() noexcept;
	[[nodiscard]] bool NeedsFrame() noexcept;
	[[nodiscard]] bool HasSettingsPages() noexcept;
	[[nodiscard]] bool IsMenuVisible() noexcept;
	[[nodiscard]] DMUI_Result SetMenuVisible(bool a_visible) noexcept;
	void CloseMenu() noexcept;
	[[nodiscard]] DMUI_PageHandle SelectedPage() noexcept;
	void ClearPageSelection(DMUI_PageHandle a_page) noexcept;
	[[nodiscard]] bool DrawPage(DMUI_PageHandle a_page) noexcept;
	[[nodiscard]] bool PageFailed(DMUI_PageHandle a_page) noexcept;
	[[nodiscard]] bool InvokeAction(DMUI_ActionHandle a_action) noexcept;
	[[nodiscard]] bool ActionFailed(DMUI_ActionHandle a_action) noexcept;
	void DrawDemandedOverlays() noexcept;
	[[nodiscard]] const std::vector<RegisteredPage>& OrderedPages() noexcept;
	[[nodiscard]] const std::vector<RegisteredAction>& OrderedActions() noexcept;
	[[nodiscard]] const NavigationModel& Navigation() noexcept;
	[[nodiscard]] DMUI_Result SetHostStatus(
		DMUI_StatusSeverity a_severity,
		std::string_view a_message) noexcept;
	[[nodiscard]] std::optional<StatusMessage> CurrentStatus() noexcept;
	[[nodiscard]] bool DismissStatus(uint64_t a_generation) noexcept;

	[[nodiscard]] DMUI_Result RegisterInternalClient(
		const DMUI_ClientDescriptor* a_descriptor,
		DMUI_ClientHandle* a_client) noexcept;
}
