#pragma once

// Ported from Fallout 4 Community Shaders src/Menu/CursorLoader.*, GPL-3.0.

#include <cstdint>

namespace Addictol::DearModdingUI::CursorLoader
{
	void Initialize(void* a_window) noexcept;
	void PrepareFrame(bool a_modalVisible) noexcept;
	[[nodiscard]] bool HandleWindowMessage(
		uint32_t a_message,
		uint64_t a_lparam) noexcept;
	void Shutdown() noexcept;
}
