#pragma once

#include <DearModdingUI/ThemeDefaults.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
		inline const ImVec4 kSuccess{ 0.0f, 1.0f, 0.0f, 1.0f };
		inline const ImVec4 kWarning{ 1.0f, 0.6f, 0.2f, 1.0f };
		inline const ImVec4 kError{ 1.0f, 0.4f, 0.4f, 1.0f };
		inline const ImVec4 kInfo{ 0.2f, 1.0f, 0.328f, 1.0f };
		inline const ImVec4 kMuted{ 0.5f, 0.5f, 0.5f, 1.0f };

		[[nodiscard]] ImVec4 Accent() noexcept;
		[[nodiscard]] ImVec4 AccentMuted() noexcept;
	}

	void Initialize(void* a_window) noexcept;
	[[nodiscard]] bool PrepareFrame(uint32_t a_backBufferHeight) noexcept;
	void ApplyStyle() noexcept;
	[[nodiscard]] const Fonts& GetFonts() noexcept;
	[[nodiscard]] float Scale() noexcept;
	[[nodiscard]] float SearchScale() noexcept;
	[[nodiscard]] ImVec4 IconTint() noexcept;
	[[nodiscard]] const std::vector<std::string>& AvailableBodyFontFamilies() noexcept;
	[[nodiscard]] std::string_view ResolveBodyFontFamily(
		std::string_view a_requested) noexcept;
	[[nodiscard]] std::string_view EffectiveBodyFontFamily() noexcept;
}

namespace Addictol
{
	namespace Theme = DearModdingUI::Theme;
}
