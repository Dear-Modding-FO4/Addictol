#pragma once

// Ported from Fallout 4 Community Shaders src/Menu/ThemeManager.* and Fonts.*, GPL-3.0.

#include <DearModdingUI/ThemeDefaults.h>

#include <cstdint>

namespace Addictol::DearModdingUI::Theme
{
	struct Fonts
	{
		ImFont* body{ nullptr };
		ImFont* title{ nullptr };
		ImFont* heading{ nullptr };
		ImFont* subheading{ nullptr };
		ImFont* subtext{ nullptr };
	};

	class FontGuard
	{
	public:
		explicit FontGuard(FontRole a_role) noexcept;
		~FontGuard() noexcept;

		FontGuard(const FontGuard&) = delete;
		FontGuard& operator=(const FontGuard&) = delete;

	private:
		bool m_pushed{ false };
	};

	namespace colors
	{
		inline const ImVec4 kAccent{ 0.26f, 0.98f, 0.3752f, 1.0f };
		inline const ImVec4 kAccentMuted{ 0.26f, 0.98f, 0.3752f, 0.39f };
		inline const ImVec4 kSuccess{ 0.0f, 1.0f, 0.0f, 1.0f };
		inline const ImVec4 kWarning{ 1.0f, 0.6f, 0.2f, 1.0f };
		inline const ImVec4 kError{ 1.0f, 0.4f, 0.4f, 1.0f };
		inline const ImVec4 kInfo{ 0.2f, 1.0f, 0.328f, 1.0f };
		inline const ImVec4 kMuted{ 0.5f, 0.5f, 0.5f, 1.0f };
	}

	void Initialize(void* a_window) noexcept;
	[[nodiscard]] bool PrepareFrame(uint32_t a_backBufferHeight) noexcept;
	void ApplyStyle() noexcept;
	[[nodiscard]] const Fonts& GetFonts() noexcept;
	[[nodiscard]] float Scale() noexcept;
	[[nodiscard]] float SearchScale() noexcept;
}

namespace Addictol
{
	namespace Theme = DearModdingUI::Theme;
}
