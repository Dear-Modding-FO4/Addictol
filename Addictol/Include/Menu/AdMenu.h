#pragma once

#include <Menu/AdMenuTargets.h>

#include <REX/REX.h>

#include <DearModdingUI/Client.h>
#include <DearModdingUI/UI.h>

#include <cstdint>

namespace Addictol::Menu
{
	using PanelDraw = MenuPanelDraw;

	inline constexpr const char* kClientIconName{ "pill" };
	inline constexpr dmui::ClientOptions kClientOptions{
		.requiredServices =
			DMUI_HOST_SERVICE_EXTERNAL_OPEN |
			DMUI_HOST_SERVICE_NAVIGATION_ICONS,
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
		.sortKey = 100,
		.iconName = "pulse"
	};
	inline constexpr dmui::PageDescriptor kHomePage{
		.id = "home",
		.displayName = "Home",
		.categoryId = kGeneralCategory.id,
		.summary = "Overview, live module status, project links, and FAQ.",
		.sortKey = 0,
		.kind = DMUI_PAGE_KIND_SETTINGS,
		.iconName = "house"
	};
	inline constexpr dmui::PageDescriptor kSettingsPage{
		.id = "settings",
		.displayName = "Settings",
		.categoryId = kGeneralCategory.id,
		.summary =
			"Configure Addictol fixes, performance, visuals, gameplay, and diagnostics.",
		.sortKey = 100,
		.kind = DMUI_PAGE_KIND_SETTINGS,
		.iconName = "sliders-horizontal"
	};
	inline constexpr dmui::PageDescriptor kModulesPage{
		.id = "modules",
		.displayName = "Modules",
		.categoryId = kGeneralCategory.id,
		.summary = "Individual install, disable, skip, and failure outcomes for every module.",
		.sortKey = 200,
		.kind = DMUI_PAGE_KIND_SETTINGS,
		.iconName = "puzzle-piece"
	};
	inline constexpr dmui::PageDescriptor kFacegenExceptionsPage{
		.id = "facegen-exceptions",
		.displayName = "Facegen Exceptions",
		.categoryId = kDiagnosticsCategory.id,
		.summary = "Facegen exception coverage, configuration state, and resolution failures.",
		.sortKey = 900,
		.kind = DMUI_PAGE_KIND_SETTINGS,
		.iconName = "files"
	};
	inline constexpr dmui::PageDescriptor kLogControlPage{
		.id = "log-control",
		.displayName = "Log Control",
		.categoryId = kDiagnosticsCategory.id,
		.summary = "Runtime logging levels and output statistics.",
		.sortKey = 1000,
		.kind = DMUI_PAGE_KIND_SETTINGS,
		.iconName = "terminal"
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
		dmui::PageDescriptor page;
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
