#pragma once

#include <Menu/AdMenuTargets.h>

#include <REX/REX.h>

#include <DearModdingUI/Client.h>
#include <DearModdingUI/ImGuiForward.h>

#include <cstdint>

namespace Addictol::Menu
{
	using PanelDraw = MenuPanelDraw;

	struct Panel
	{
		const char* id;
		const char* name;
		const char* category;
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
	void ReportStatus(
		DMUI_StatusSeverity a_severity,
		const char* a_message) noexcept;

	void FinalizeRegistration() noexcept;

	[[nodiscard]] uint32_t RefreshMs() noexcept;
}
