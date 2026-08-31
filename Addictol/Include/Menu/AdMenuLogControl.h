#pragma once

#include <string_view>

namespace Addictol
{
	using namespace std::literals;

	inline constexpr std::string_view kMenuLogControlPanelName{ "Log Control"sv };

	void DrawMenuLogControlPanel(void*) noexcept;
}
