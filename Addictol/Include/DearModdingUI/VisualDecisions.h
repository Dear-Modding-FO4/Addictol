#pragma once

#include <DearModdingUI/ThemeDefaults.h>

#include <cstdint>

namespace Addictol::DearModdingUI
{
	[[nodiscard]] inline float ResolveUiScale(
		[[maybe_unused]] float a_dpiScale,
		uint32_t a_backBufferHeight) noexcept
	{
		return Theme::ResolveRoleFontSize(
				   Theme::FontRole::kBody,
				   a_backBufferHeight) /
			Theme::kBaselineFontSize;
	}

	struct CursorPresentation
	{
		bool captureInput{ false };
		bool hideOperatingSystemCursor{ false };
		bool drawSoftwareCursor{ false };
		bool drawCustomCursor{ false };
	};

	[[nodiscard]] constexpr CursorPresentation DecideCursorPresentation(
		bool a_modalVisible) noexcept
	{
		if (!a_modalVisible)
			return {};
		return {
			true,
			true,
			true,
			false
		};
	}

	enum class CursorOwnershipTransition : uint32_t
	{
		kNone,
		kAcquire,
		kRelease
	};

	[[nodiscard]] constexpr CursorOwnershipTransition DecideCursorTransition(
		bool a_owned,
		bool a_modalVisible) noexcept
	{
		if (a_owned == a_modalVisible)
			return CursorOwnershipTransition::kNone;
		return a_modalVisible ?
			CursorOwnershipTransition::kAcquire :
			CursorOwnershipTransition::kRelease;
	}

	struct InlineIconLayout
	{
		bool drawIcon{ false };
		float iconSize{ 0.0f };
		float textOffset{ 0.0f };
		float contentWidth{ 0.0f };
		float contentHeight{ 0.0f };
	};

	[[nodiscard]] constexpr InlineIconLayout DecideInlineIconLayout(
		bool a_hasIcon,
		float a_textWidth,
		float a_textHeight,
		float a_fontSize,
		float a_spacing) noexcept
	{
		const auto textWidth = a_textWidth > 0.0f ? a_textWidth : 0.0f;
		const auto textHeight = a_textHeight > 0.0f ? a_textHeight : 0.0f;
		const auto iconSize = a_hasIcon && a_fontSize > 0.0f ? a_fontSize : 0.0f;
		const auto spacing = iconSize > 0.0f && a_spacing > 0.0f ? a_spacing : 0.0f;
		return {
			iconSize > 0.0f,
			iconSize,
			iconSize + spacing,
			textWidth + iconSize + spacing,
			textHeight > iconSize ? textHeight : iconSize
		};
	}
}
