#pragma once

#include <Menu/AdMenuTargets.h>

#include <REX/REX.h>

#include <DearModdingUI/Client.h>
#include <DearModdingUI/UI.h>

#include <cstdint>

namespace Addictol::Menu
{
	using PanelDraw = MenuPanelDraw;

	inline constexpr dmui::ClientOptions kClientOptions{
		.requiredServices = DMUI_HOST_SERVICE_EXTERNAL_OPEN,
		.minimumUIAPISize = DMUI_UI_API_PLOT_LINES_SIZE
	};
	inline constexpr dmui::CategoryDescriptor kGeneralCategory{
		.id = "general",
		.displayName = "General",
		.sortKey = 0
	};
	inline constexpr dmui::CategoryDescriptor kDiagnosticsCategory{
		.id = "diagnostics",
		.displayName = "Diagnostics",
		.sortKey = 100
	};
	inline constexpr dmui::TextStyle kHeadingText{
		.fontRole = DMUI_FONT_ROLE_HEADING,
		.tone = dmui::TextTone::kAccent
	};
	inline constexpr dmui::TextStyle kBodyText{ .fontRole = DMUI_FONT_ROLE_BODY };
	inline constexpr dmui::TextStyle kMutedText{
		.fontRole = DMUI_FONT_ROLE_SUBTEXT,
		.tone = dmui::TextTone::kMuted
	};
	inline constexpr dmui::ui::TableFlags kDiagnosticTableFlags =
		dmui::ui::TableFlags::kResizable | dmui::ui::TableFlags::kRowBg |
		dmui::ui::TableFlags::kBordersInner | dmui::ui::TableFlags::kBordersOuter | dmui::ui::TableFlags::kScrollY;
	inline constexpr dmui::ui::TableFlags kStaticDiagnosticTableFlags =
		static_cast<dmui::ui::TableFlags>(
			static_cast<uint32_t>(kDiagnosticTableFlags) &
			~static_cast<uint32_t>(dmui::ui::TableFlags::kScrollY));

	struct Panel
	{
		const char* id;
		const char* name;
		const char* categoryId;
		const char* summary;
		int32_t sortKey;
		PanelDraw draw;
		const REX::TOML::Bool<>* gate;
		void* context;
	};

	[[nodiscard]] bool RegisterPanel(const Panel& a_panel) noexcept;

	[[nodiscard]] bool Install() noexcept;
	[[nodiscard]] dmui::Client& Client() noexcept;
	[[nodiscard]] const DMUI_ThemeColors& ThemeColors() noexcept;
	void ReportPresentationResult(bool a_succeeded) noexcept;
	[[nodiscard]] std::optional<DMUI_StyleMetrics> StyleMetrics() noexcept;
	void ReportStatus(
		DMUI_StatusSeverity a_severity,
		const char* a_message) noexcept;

	void FinalizeRegistration() noexcept;

	[[nodiscard]] uint32_t RefreshMs() noexcept;
}
