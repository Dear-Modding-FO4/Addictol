#pragma once

#include "AdMenuTargets.h"

#include <REX/REX.h>

#include <cstdint>
#include <string_view>

namespace Addictol::Menu
{
	using PanelDraw = MenuPanelDraw;

	struct Panel
	{
		std::string_view name;
		PanelDraw draw;
		const REX::TOML::Bool<>* gate;   // nullptr = always shown
		const void* context;
	};

	// register from a load-stage install
	[[nodiscard]] bool RegisterPanel(const Panel& a_panel) noexcept;

	[[nodiscard]] bool Install() noexcept;

	void FinalizeRegistration() noexcept;

	[[nodiscard]] uint32_t RefreshMs() noexcept;
	[[nodiscard]] std::string_view ToggleKeyName() noexcept;
	[[nodiscard]] double LastDrawMs() noexcept;
	// bumped on open so panels drop transient state
	[[nodiscard]] uint64_t OpenGeneration() noexcept;
}
