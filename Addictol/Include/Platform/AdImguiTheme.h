#pragma once

// Adapted from Fallout 4 Community Shaders src/Menu/Theme.h and src/Menu/Theme.cpp, GPL-3.0.

#include <imgui/imgui.h>

namespace Addictol::Theme
{
	struct Fonts
	{
		ImFont* body{ nullptr };
		ImFont* subtext{ nullptr };
		ImFont* title{ nullptr };
		ImFont* heading{ nullptr };
	};

	// Modifies the supplied style rather than the global one.
	void ApplyDarkTheme(ImGuiStyle& a_style) noexcept;

	[[nodiscard]] const Fonts& GetFonts() noexcept;

	// Returns false when any role fell back to the built-in font.
	bool LoadFonts(ImGuiIO& a_io, float a_dpiScale) noexcept;

	// Style, sizes, and fonts in one pass; the DPI scale is applied exactly once.
	void Apply(ImGuiIO& a_io, ImGuiStyle& a_style, float a_dpiScale) noexcept;

	namespace colors
	{
		inline const ImVec4 kAccent{ 0.000f, 0.933f, 0.000f, 1.00f };
		inline const ImVec4 kAccentMedium{ 0.000f, 0.557f, 0.000f, 1.00f };
		inline const ImVec4 kAccentDark{ 0.000f, 0.373f, 0.000f, 1.00f };
		inline const ImVec4 kAccentDeep{ 0.000f, 0.184f, 0.000f, 1.00f };
		inline const ImVec4 kSuccess{ 0.50f, 0.90f, 0.55f, 1.00f };
		inline const ImVec4 kWarning{ 1.00f, 0.78f, 0.42f, 1.00f };
		inline const ImVec4 kError{ 1.00f, 0.42f, 0.42f, 1.00f };
		inline const ImVec4 kInfo{ 0.55f, 0.80f, 1.00f, 1.00f };
		inline const ImVec4 kMuted{ 0.65f, 0.65f, 0.70f, 1.00f };
	}
}
