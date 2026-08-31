#pragma once

#include <DearModdingUI/API.h>
#include <Menu/AdMenuTargets.h>

#include <REX/REX.h>

#include <cstdint>
#include <string_view>

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
	void ReportStatus(
		DMUI_StatusSeverity a_severity,
		const char* a_message) noexcept;

	void FinalizeRegistration() noexcept;

	[[nodiscard]] uint32_t RefreshMs() noexcept;
	[[nodiscard]] std::string_view ToggleKeyName() noexcept;
	[[nodiscard]] double LastDrawMs() noexcept;
	[[nodiscard]] uint64_t OpenGeneration() noexcept;
}
