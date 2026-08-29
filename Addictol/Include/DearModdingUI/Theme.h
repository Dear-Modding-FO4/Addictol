#pragma once

// Adapted from Fallout 4 Community Shaders src/Menu/ThemeManager.* and Fonts.*, GPL-3.0.

#include <imgui/imgui.h>
#include <DearModdingUI/VisualDecisions.h>

#include <cstdint>

namespace Addictol::DearModdingUI::Theme
{
	enum class FontRole : uint32_t
	{
		kBody,
		kTitle,
		kHeading,
		kSubtext,
		kMonospace
	};

	struct Fonts
	{
		ImFont* body{ nullptr };
		ImFont* title{ nullptr };
		ImFont* heading{ nullptr };
		ImFont* subtext{ nullptr };
		ImFont* monospace{ nullptr };
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
		inline const ImVec4 kAccent{ 0.260f, 0.980f, 0.375f, 1.00f };
		inline const ImVec4 kAccentMuted{ 0.180f, 0.610f, 0.280f, 1.00f };
		inline const ImVec4 kAccentDeep{ 0.065f, 0.240f, 0.115f, 1.00f };
		inline const ImVec4 kSuccess{ 0.420f, 0.930f, 0.520f, 1.00f };
		inline const ImVec4 kWarning{ 1.000f, 0.700f, 0.300f, 1.00f };
		inline const ImVec4 kError{ 1.000f, 0.400f, 0.400f, 1.00f };
		inline const ImVec4 kInfo{ 0.430f, 0.760f, 1.000f, 1.00f };
		inline const ImVec4 kMuted{ 0.670f, 0.710f, 0.690f, 1.00f };
		inline const ImVec4 kSidebar{ 0.025f, 0.030f, 0.034f, 0.88f };
		inline const ImVec4 kContent{ 0.035f, 0.040f, 0.044f, 0.72f };
	}

	void Initialize(void* a_window) noexcept;
	[[nodiscard]] bool PrepareFrame(uint32_t a_backBufferHeight) noexcept;
	[[nodiscard]] const Fonts& GetFonts() noexcept;
	[[nodiscard]] float Scale() noexcept;
}

namespace Addictol
{
	namespace Theme = DearModdingUI::Theme;
}
