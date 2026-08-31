#pragma once

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol::DearModdingUI::Theme
{
	inline constexpr float kDefaultScreenHeight{ 1080.0f };
	inline constexpr float kDefaultFontRatio{ 7.0f / 360.0f };
	inline constexpr float kBaselineFontSize{ kDefaultScreenHeight * kDefaultFontRatio };
	inline constexpr float kSearchBaselineFontSize{ 1440.0f * kDefaultFontRatio };
	inline constexpr float kMinFontSize{ 16.0f };
	inline constexpr float kMaxFontSize{ 108.0f };
	inline constexpr float kDefaultFontSize{ 27.0f };
	inline constexpr float kDefaultGlobalScale{ 0.0f };
	inline constexpr float kMinUserScale{ 0.75f };
	inline constexpr float kMaxUserScale{ 2.0f };
	inline constexpr float kDefaultUserScale{ 1.0f };
	inline constexpr ImVec4 kDefaultAccent{
		66.0f / 255.0f,
		250.0f / 255.0f,
		96.0f / 255.0f,
		1.0f
	};

	enum class IconColorMode : uint32_t
	{
		kColored,
		kMonochrome
	};

	struct IconDefaults
	{
		IconColorMode colorMode{ IconColorMode::kColored };
		float baselineOffsetRatio{ 4.0f / 21.0f };
	};

	inline constexpr float kHeaderBaseTextScale{ 1.7f };
	inline constexpr float kHeaderFallbackTextScale{ 1.5f };
	inline constexpr float kSeparatorThickness{ 3.0f };
	inline constexpr float kFeatureTitleScale{ 1.5f };
	inline constexpr float kVersionTextOpacity{ 0.6f };
	inline constexpr float kSearchIconSize{ 20.0f };
	inline constexpr float kSearchIconAlpha{ 0.7f };
	inline constexpr float kSearchIconOffsetX{ 8.0f };
	inline constexpr float kSearchInputPaddingExtra{ 14.0f };
	inline constexpr float kSearchInputFramePaddingY{ 6.0f };
	inline constexpr float kSearchIconStrokeRatio{ 0.11f };
	inline constexpr float kSearchIconHandleStrokeRatio{ 0.105f };

	enum class FontRole : uint32_t
	{
		kBody,
		kTitle,
		kHeading,
		kSubheading,
		kSubtext,
		kCount
	};

	struct FontRoleDefaults
	{
		std::string_view key;
		std::string_view family;
		std::string_view style;
		std::string_view file;
		float sizeScale;
	};

	inline constexpr std::array<FontRoleDefaults, static_cast<std::size_t>(FontRole::kCount)>
		kFontRoleDefaults{
			FontRoleDefaults{ "body", "Jost", "Regular", "Jost\\Jost-Regular.ttf", 1.0f },
			FontRoleDefaults{ "title", "Jost", "SemiBold", "Jost\\Jost-SemiBold.ttf", 1.3f },
			FontRoleDefaults{ "heading", "Jost", "Regular", "Jost\\Jost-Regular.ttf", 1.0f },
			FontRoleDefaults{ "subheading", "Jost", "Regular", "Jost\\Jost-Regular.ttf", 1.0f },
			FontRoleDefaults{ "subtext", "Jost", "Regular", "Jost\\Jost-Regular.ttf", 0.9f }
		};

	struct StyleDefaults
	{
		float windowBorderSize{ 2.0f };
		float childBorderSize{ 0.0f };
		float frameBorderSize{ 1.0f };
		ImVec2 windowPadding{ 8.0f, 8.0f };
		float windowRounding{ 12.0f };
		float indentSpacing{ 8.0f };
		ImVec2 framePadding{ 8.0f, 4.0f };
		ImVec2 cellPadding{ 8.0f, 2.0f };
		ImVec2 itemSpacing{ 4.0f, 8.0f };
		float frameRounding{ 4.0f };
		float tabRounding{ 4.0f };
		float scrollbarRounding{ 9.0f };
		float scrollbarSize{ 12.0f };
		float grabRounding{ 3.0f };
		float grabMinSize{ 12.0f };
	};

	struct ScrollbarOpacityDefaults
	{
		float background{ 0.0f };
		float thumb{ 0.5f };
		float thumbHovered{ 0.75f };
		float thumbActive{ 0.9f };
	};

	struct StatusPaletteDefaults
	{
		ImVec4 disable{ 0.5f, 0.5f, 0.5f, 1.0f };
		ImVec4 error{ 1.0f, 0.4f, 0.4f, 1.0f };
		ImVec4 warning{ 1.0f, 0.6f, 0.2f, 1.0f };
		ImVec4 restartNeeded{ 0.4f, 1.0f, 0.4f, 1.0f };
		ImVec4 currentHotkey{ 1.0f, 1.0f, 0.0f, 1.0f };
		ImVec4 success{ 0.0f, 1.0f, 0.0f, 1.0f };
		ImVec4 info{ 0.2f, 1.0f, 0.328f, 1.0f };
	};

	struct FeatureHeadingDefaults
	{
		ImVec4 colorDefault{ 0.8f, 0.8f, 0.8f, 1.0f };
		ImVec4 colorHovered{ 0.6f, 0.6f, 0.6f, 1.0f };
		float minimizedFactor{ 0.7f };
		float titleScale{ 1.5f };
	};

	struct CursorDefaults
	{
		struct Image
		{
			std::string_view file;
			float hotspotX{ 0.0f };
			float hotspotY{ 0.0f };
		};

		float scale{ 1.0f };
		bool useCustomCursor{ false };
		std::array<Image, ImGuiMouseCursor_COUNT> types{};
	};

	inline constexpr StyleDefaults kStyleDefaults{};
	inline constexpr IconDefaults kIconDefaults{};
	inline constexpr ScrollbarOpacityDefaults kScrollbarOpacityDefaults{};
	inline constexpr StatusPaletteDefaults kStatusPaletteDefaults{};
	inline constexpr FeatureHeadingDefaults kFeatureHeadingDefaults{};
	inline constexpr CursorDefaults kCursorDefaults{};
	inline constexpr float kTooltipHoverDelay{ 0.1f };

	static_assert(ImGuiCol_COUNT == 63);
	inline const std::array<ImVec4, ImGuiCol_COUNT> kFullPalette{
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		ImVec4(1.0f, 1.0f, 1.0f, 0.3f),
		ImVec4(0.03f, 0.03f, 0.03f, 0.55f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
		ImVec4(0.05f, 0.05f, 0.1f, 0.85f),
		ImVec4(0.5f, 0.5f, 0.5f, 0.8f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
		ImVec4(0.4f, 0.4f, 0.4f, 0.7f),
		ImVec4(0.26f, 0.26f, 0.26f, 0.4f),
		ImVec4(0.4f, 0.4f, 0.4f, 0.45f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.83f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.87f),
		ImVec4(0.2f, 0.2f, 0.3f, 0.9f),
		ImVec4(0.02f, 0.02f, 0.03f, 0.9f),
		ImVec4(0.2f, 0.22f, 0.27f, 0.9f),
		ImVec4(0.28f, 0.28f, 0.28f, 1.0f),
		ImVec4(0.42f, 0.42f, 0.42f, 1.0f),
		ImVec4(0.56f, 0.56f, 0.56f, 1.0f),
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		ImVec4(0.31f, 0.31f, 0.31f, 0.5f),
		ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
		ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.39f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.2f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),
		ImVec4(0.06f, 0.98f, 0.2072f, 0.39f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.2f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.59f),
		ImVec4(0.5f, 0.5f, 0.5f, 0.6f),
		ImVec4(0.7f, 0.6f, 0.6f, 1.0f),
		ImVec4(0.9f, 0.7f, 0.7f, 1.0f),
		ImVec4(0.6f, 0.6f, 0.6f, 0.8f),
		ImVec4(0.6f, 0.6f, 0.6f, 0.1f),
		ImVec4(0.6f, 0.6f, 0.6f, 0.1f),
		ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.31f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.8f),
		ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
		ImVec4(0.38f, 0.83f, 0.452f, 1.0f),
		ImVec4(0.15f, 0.15f, 0.15f, 0.97f),
		ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
		ImVec4(0.5f, 0.5f, 0.5f, 0.0f),
		ImVec4(0.7f, 0.6f, 0.6f, 0.5f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
		ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
		ImVec4(0.9f, 0.7f, 0.0f, 1.0f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.4f),
		ImVec4(0.26f, 0.26f, 0.26f, 1.0f),
		ImVec4(0.19f, 0.19f, 0.19f, 1.0f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
		ImVec4(1.0f, 1.0f, 1.0f, 0.06f),
		ImVec4(0.38f, 0.83f, 0.452f, 1.0f),
		ImVec4(0.26f, 0.98f, 0.3752f, 0.35f),
		ImVec4(0.7f, 0.7f, 0.7f, 0.65f),
		ImVec4(0.8f, 0.5f, 0.5f, 1.0f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		ImVec4(0.26f, 0.98f, 0.3752f, 1.0f),
		ImVec4(0.3f, 0.3f, 0.3f, 0.56f),
		ImVec4(0.2f, 0.2f, 0.2f, 0.35f),
		ImVec4(0.2f, 0.2f, 0.2f, 0.35f)
	};

	[[nodiscard]] inline std::array<ImVec4, ImGuiCol_COUNT>
		MakeEffectivePalette(
			const ImVec4& a_accent = kDefaultAccent,
			float a_windowOpacity = 0.55f) noexcept
	{
		auto palette = kFullPalette;
		const auto accent = ImVec4{
			std::clamp(a_accent.x, 0.0f, 1.0f),
			std::clamp(a_accent.y, 0.0f, 1.0f),
			std::clamp(a_accent.z, 0.0f, 1.0f),
			1.0f
		};
		constexpr ImGuiCol accentColors[]{
			ImGuiCol_CheckboxSelectedBg,
			ImGuiCol_SliderGrab,
			ImGuiCol_SliderGrabActive,
			ImGuiCol_Button,
			ImGuiCol_ButtonHovered,
			ImGuiCol_ButtonActive,
			ImGuiCol_Header,
			ImGuiCol_HeaderHovered,
			ImGuiCol_HeaderActive,
			ImGuiCol_InputTextCursor,
			ImGuiCol_TabHovered,
			ImGuiCol_Tab,
			ImGuiCol_TabSelected,
			ImGuiCol_TabSelectedOverline,
			ImGuiCol_TabDimmedSelected,
			ImGuiCol_TabDimmedSelectedOverline,
			ImGuiCol_DockingPreview,
			ImGuiCol_TextLink,
			ImGuiCol_TextSelectedBg,
			ImGuiCol_DragDropTarget,
			ImGuiCol_DragDropTargetBg,
			ImGuiCol_UnsavedMarker,
			ImGuiCol_NavCursor
		};
		for (const auto color : accentColors)
		{
			palette[color].x = accent.x;
			palette[color].y = accent.y;
			palette[color].z = accent.z;
		}
		palette[ImGuiCol_WindowBg].w =
			std::isfinite(a_windowOpacity) ?
				std::clamp(a_windowOpacity, 0.0f, 1.0f) :
				0.55f;
		palette[ImGuiCol_ScrollbarBg].w =
			kScrollbarOpacityDefaults.background;
		palette[ImGuiCol_ScrollbarGrab].w =
			kScrollbarOpacityDefaults.thumb;
		palette[ImGuiCol_ScrollbarGrabHovered].w =
			kScrollbarOpacityDefaults.thumbHovered;
		palette[ImGuiCol_ScrollbarGrabActive].w =
			kScrollbarOpacityDefaults.thumbActive;
		return palette;
	}

	[[nodiscard]] constexpr ImVec4 ResolveIconTint(
		IconColorMode a_mode,
		const ImVec4& a_accent,
		const ImVec4& a_text) noexcept
	{
		return a_mode == IconColorMode::kColored ? a_accent : a_text;
	}

	[[nodiscard]] inline ImGuiStyle MakeBaseStyle() noexcept
	{
		ImGuiStyle style{};
		style.WindowBorderSize = kStyleDefaults.windowBorderSize;
		style.ChildBorderSize = kStyleDefaults.childBorderSize;
		style.FrameBorderSize = kStyleDefaults.frameBorderSize;
		style.WindowPadding = kStyleDefaults.windowPadding;
		style.WindowRounding = kStyleDefaults.windowRounding;
		style.IndentSpacing = kStyleDefaults.indentSpacing;
		style.FramePadding = kStyleDefaults.framePadding;
		style.CellPadding = kStyleDefaults.cellPadding;
		style.ItemSpacing = kStyleDefaults.itemSpacing;
		style.FrameRounding = kStyleDefaults.frameRounding;
		style.TabRounding = kStyleDefaults.tabRounding;
		style.ScrollbarRounding = kStyleDefaults.scrollbarRounding;
		style.ScrollbarSize = kStyleDefaults.scrollbarSize;
		style.GrabRounding = kStyleDefaults.grabRounding;
		style.GrabMinSize = kStyleDefaults.grabMinSize;
		return style;
	}

	[[nodiscard]] inline float ResolveFontSize(uint32_t a_backBufferHeight) noexcept
	{
		const auto height = a_backBufferHeight ?
			static_cast<float>(a_backBufferHeight) :
			kDefaultScreenHeight;
		return std::clamp(height * kDefaultFontRatio, kMinFontSize, kMaxFontSize);
	}

	[[nodiscard]] inline float ResolveRoleFontSize(
		FontRole a_role,
		uint32_t a_backBufferHeight,
		float a_userScale = kDefaultUserScale) noexcept
	{
		const auto index = static_cast<std::size_t>(a_role);
		const auto role = index < kFontRoleDefaults.size() ?
			kFontRoleDefaults[index].sizeScale :
			1.0f;
		const auto userScale = std::isfinite(a_userScale) ?
			std::clamp(a_userScale, kMinUserScale, kMaxUserScale) :
			kDefaultUserScale;
		return std::round(std::clamp(
			ResolveFontSize(a_backBufferHeight) * userScale * role,
			kMinFontSize * kMinUserScale,
			kMaxFontSize * kMaxUserScale));
	}

	[[nodiscard]] inline float ResolveStyleScale(
		float a_bodyFontSize,
		float a_globalScale = kDefaultGlobalScale) noexcept
	{
		const auto body = std::isfinite(a_bodyFontSize) && a_bodyFontSize > 0.0f ?
			a_bodyFontSize :
			kBaselineFontSize;
		const auto global = std::isfinite(a_globalScale) ?
			a_globalScale :
			kDefaultGlobalScale;
		return body / kBaselineFontSize * std::exp2(global);
	}
}
