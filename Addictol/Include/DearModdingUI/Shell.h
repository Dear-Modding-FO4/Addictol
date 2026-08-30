#pragma once

#include <cstddef>

namespace Addictol::DearModdingUI
{
	void DrawSectionHeader(const char* a_text, char32_t a_glyph = 0) noexcept;
	void DrawCollapsingSectionHeader(
		const char* a_key,
		const char* a_text,
		char32_t a_glyph,
		bool& a_expanded,
		size_t a_count) noexcept;
	void DrawShell() noexcept;
}
