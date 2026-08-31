#pragma once

#include <DearModdingUI/SettingsActions.h>

#include <imgui/imgui.h>

#include <cstddef>
#include <string>

namespace Addictol::DearModdingUI
{
	[[nodiscard]] float SettingsActionButtonExtent() noexcept;
	[[nodiscard]] float SettingsActionButtonWidth(
		SettingsAction a_action,
		const char* a_fallbackLabel,
		float a_buttonExtent) noexcept;
	[[nodiscard]] bool DrawSettingsActionButton(
		const char* a_id,
		const ImVec2& a_origin,
		const ImVec2& a_size,
		SettingsAction a_action,
		const char* a_fallbackLabel,
		const char* a_tooltip,
		bool a_enabled) noexcept;
	void DrawSearchInput(
		const char* a_id,
		const char* a_hint,
		std::string& a_search) noexcept;
	void DrawSectionHeader(const char* a_text, char32_t a_glyph = 0) noexcept;
	void DrawCollapsingSectionHeader(
		const char* a_key,
		const char* a_text,
		char32_t a_glyph,
		bool& a_expanded,
		size_t a_count) noexcept;
	void DrawShell() noexcept;
}
