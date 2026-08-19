#pragma once

#include <string_view>

namespace Addictol
{
	using namespace std::literals;

	// pinned after contributed panels
	inline constexpr std::string_view kMenuLogControlPanelName{ "Log Control"sv };

	void DrawMenuLogControlPanel(const void*) noexcept;
}
