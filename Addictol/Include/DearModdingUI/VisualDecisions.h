#pragma once

#include <DearModdingUI/ThemeDefaults.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI
{
	[[nodiscard]] inline float ResolveUiScale(
		[[maybe_unused]] float a_dpiScale,
		uint32_t a_backBufferHeight,
		float a_userScale = Theme::kDefaultUserScale) noexcept
	{
		return Theme::ResolveRoleFontSize(
				   Theme::FontRole::kBody,
				   a_backBufferHeight,
				   a_userScale) /
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

	struct GlyphDrawOrigin
	{
		float x{ 0.0f };
		float y{ 0.0f };

		constexpr bool operator==(const GlyphDrawOrigin&) const noexcept = default;
	};

	[[nodiscard]] constexpr GlyphDrawOrigin ResolveCenteredGlyphOrigin(
		float a_centerX,
		float a_centerY,
		float a_glyphMinX,
		float a_glyphMinY,
		float a_glyphMaxX,
		float a_glyphMaxY,
		float a_scale) noexcept
	{
		const auto scale = a_scale > 0.0f ? a_scale : 0.0f;
		return {
			a_centerX - (a_glyphMinX + a_glyphMaxX) * scale * 0.5f,
			a_centerY - (a_glyphMinY + a_glyphMaxY) * scale * 0.5f
		};
	}

	// Leftmost bullet ink: ImGui centers it at FramePadding.x + FontSize/2 with radius FontSize/5.
	[[nodiscard]] constexpr float BulletRunContentInset(
		float a_framePaddingX,
		float a_fontSize) noexcept
	{
		const auto framePaddingX = a_framePaddingX > 0.0f ?
			a_framePaddingX :
			0.0f;
		const auto fontSize = a_fontSize > 0.0f ? a_fontSize : 0.0f;
		return framePaddingX + fontSize * 0.3f;
	}

	[[nodiscard]] constexpr float CenterOffsetY(
		float a_rowHeight,
		float a_contentHeight) noexcept
	{
		const auto rowHeight = a_rowHeight > 0.0f ? a_rowHeight : 0.0f;
		const auto contentHeight =
			a_contentHeight > 0.0f ? a_contentHeight : 0.0f;
		return (std::max)(
			(rowHeight - contentHeight) * 0.5f,
			0.0f);
	}

	[[nodiscard]] constexpr float OpticalTextOffsetY(
		float a_rowHeight,
		float a_fontSize,
		float a_referenceMinY,
		float a_referenceMaxY,
		float a_scale) noexcept
	{
		const auto boxOffset = CenterOffsetY(a_rowHeight, a_fontSize);
		if (a_rowHeight <= 0.0f ||
			a_fontSize <= 0.0f ||
			a_referenceMaxY <= a_referenceMinY ||
			a_scale <= 0.0f)
			return boxOffset;
		const auto origin = ResolveCenteredGlyphOrigin(
			0.0f,
			a_rowHeight * 0.5f,
			0.0f,
			a_referenceMinY,
			0.0f,
			a_referenceMaxY,
			a_scale);
		return origin.y;
	}

	[[nodiscard]] constexpr float FooterRowAdjustmentY(
		float a_verticalSpacing,
		float a_windowPadding) noexcept
	{
		const auto verticalSpacing = a_verticalSpacing > 0.0f ?
			a_verticalSpacing :
			0.0f;
		const auto windowPadding = a_windowPadding > 0.0f ?
			a_windowPadding :
			0.0f;
		return windowPadding - verticalSpacing;
	}

	[[nodiscard]] inline std::string BuildHostBreadcrumb(
		std::string_view a_hostName,
		std::string_view a_clientName)
	{
		std::string breadcrumb{ a_hostName };
		if (!a_clientName.empty())
		{
			breadcrumb.append(" > ");
			breadcrumb.append(a_clientName);
		}
		return breadcrumb;
	}

	[[nodiscard]] constexpr bool ShouldDrawHeaderClose(
		bool a_docked,
		bool a_noTitleBar) noexcept
	{
		return !a_docked && a_noTitleBar;
	}

	struct TrailingControlLayout
	{
		float adjacentMinX{ 0.0f };
		float adjacentMaxX{ 0.0f };
		float controlMinX{ 0.0f };
		float controlMaxX{ 0.0f };
	};

	[[nodiscard]] constexpr TrailingControlLayout ResolveTrailingControlLayout(
		float a_contentMinX,
		float a_contentMaxX,
		float a_controlExtent,
		float a_spacing) noexcept
	{
		const auto contentMax = a_contentMaxX > a_contentMinX ?
			a_contentMaxX :
			a_contentMinX;
		const auto extent = a_controlExtent > 0.0f ? a_controlExtent : 0.0f;
		const auto spacing = a_spacing > 0.0f ? a_spacing : 0.0f;
		const auto unboundedControlMin = contentMax - extent;
		const auto controlMin = unboundedControlMin > a_contentMinX ?
			unboundedControlMin :
			a_contentMinX;
		const auto unboundedAdjacentMax = controlMin - spacing;
		const auto adjacentMax = unboundedAdjacentMax > a_contentMinX ?
			unboundedAdjacentMax :
			a_contentMinX;
		return {
			a_contentMinX,
			adjacentMax,
			controlMin,
			contentMax
		};
	}

	struct FooterStatusLayout
	{
		float metadataMinX{ 0.0f };
		float metadataMaxX{ 0.0f };
		float statusMinX{ 0.0f };
		float statusMaxX{ 0.0f };
		float dismissMinX{ 0.0f };
		float dismissMaxX{ 0.0f };
		float settingsMinX{ 0.0f };
		float settingsMaxX{ 0.0f };
		float rowHeight{ 0.0f };
		float footerHeight{ 0.0f };

		constexpr bool operator==(const FooterStatusLayout&) const noexcept = default;
	};

	[[nodiscard]] constexpr float ReservedFooterHeight(
		float a_rowHeight,
		float a_verticalSpacing,
		float a_windowPadding,
		float a_separatorThickness) noexcept
	{
		const auto rowHeight = a_rowHeight > 0.0f ? a_rowHeight : 0.0f;
		const auto verticalSpacing = a_verticalSpacing > 0.0f ?
			a_verticalSpacing :
			0.0f;
		const auto windowPadding = a_windowPadding > 0.0f ?
			a_windowPadding :
			0.0f;
		const auto separator = a_separatorThickness > 0.0f ?
			a_separatorThickness :
			0.0f;
		return rowHeight +
			verticalSpacing * 2.0f +
			windowPadding +
			separator;
	}

	[[nodiscard]] constexpr FooterStatusLayout ResolveFooterStatusLayout(
		float a_contentMinX,
		float a_contentMaxX,
		float a_settingsWidth,
		float a_metadataMaxX,
		float a_statusTextWidth,
		float a_dismissWidth,
		float a_horizontalSpacing,
		float a_rowHeight,
		float a_verticalSpacing,
		float a_windowPadding,
		float a_separatorThickness,
		bool a_hasStatus,
		bool a_persistentStatus) noexcept
	{
		const auto trailing = ResolveTrailingControlLayout(
			a_contentMinX,
			a_contentMaxX,
			a_settingsWidth,
			a_horizontalSpacing);
		const auto rowHeight = a_rowHeight > 0.0f ? a_rowHeight : 0.0f;
		const auto spacing = a_horizontalSpacing > 0.0f ?
			a_horizontalSpacing :
			0.0f;
		const auto metadataLimit = trailing.adjacentMaxX;
		const auto metadataMax = metadataLimit;
		const auto metadataRight = (std::min)(
			(std::max)(a_metadataMaxX, a_contentMinX),
			metadataMax);
		const auto statusMin = (std::min)(
			metadataRight + spacing,
			metadataMax);
		const auto availableAfterMetadata = metadataMax - statusMin;
		const auto dismissWidth = (std::min)(
			a_hasStatus && a_persistentStatus && a_dismissWidth > 0.0f ?
				a_dismissWidth :
				0.0f,
			availableAfterMetadata);
		const auto dismissMax = metadataMax;
		const auto dismissMin = dismissMax - dismissWidth;
		const auto statusLimit = dismissWidth > 0.0f ?
			(std::max)(statusMin, dismissMin - spacing) :
			dismissMin;
		const auto availableStatusWidth = statusLimit - statusMin;
		const auto desiredTextWidth = a_hasStatus && a_statusTextWidth > 0.0f ?
			a_statusTextWidth :
			0.0f;
		const auto statusWidth = (std::min)(
			desiredTextWidth,
			availableStatusWidth);
		const auto statusMax = statusMin + statusWidth;
		return {
			a_contentMinX,
			metadataMax,
			statusMin,
			statusMax,
			dismissMin,
			dismissMax,
			trailing.controlMinX,
			trailing.controlMaxX,
			rowHeight,
			ReservedFooterHeight(
				rowHeight,
				a_verticalSpacing,
				a_windowPadding,
				a_separatorThickness)
		};
	}

	[[nodiscard]] constexpr float ActionButtonWidth(
		bool a_hasIcon,
		float a_textWidth,
		float a_iconExtent,
		float a_horizontalPadding) noexcept
	{
		if (a_hasIcon)
			return a_iconExtent > 0.0f ? a_iconExtent : 0.0f;
		const auto textWidth = a_textWidth > 0.0f ? a_textWidth : 0.0f;
		const auto padding = a_horizontalPadding > 0.0f ? a_horizontalPadding : 0.0f;
		return textWidth + padding * 2.0f;
	}

	template <std::size_t Size>
	[[nodiscard]] constexpr float ResolveSettingsActionButtonWidthSum(
		const std::array<float, Size>& a_widths,
		[[maybe_unused]] bool a_dirty,
		[[maybe_unused]] size_t a_pendingCount) noexcept
	{
		float sum = 0.0f;
		for (const auto width : a_widths)
		{
			if (width > 0.0f)
				sum += width;
		}
		return sum;
	}

	struct PageActionRowLayout
	{
		float titleMinX{ 0.0f };
		float titleMaxX{ 0.0f };
		float actionsMinX{ 0.0f };
		float actionsMaxX{ 0.0f };
		float reservedWidth{ 0.0f };
	};

	[[nodiscard]] constexpr PageActionRowLayout ResolvePageActionRowLayout(
		float a_contentMinX,
		float a_contentMaxX,
		float a_buttonWidthSum,
		size_t a_actionCount,
		float a_spacing) noexcept
	{
		const auto contentMax = a_contentMaxX > a_contentMinX ?
			a_contentMaxX :
			a_contentMinX;
		if (a_actionCount == 0)
		{
			return {
				a_contentMinX,
				contentMax,
				contentMax,
				contentMax,
				0.0f
			};
		}
		const auto spacing = a_spacing > 0.0f ? a_spacing : 0.0f;
		const auto buttonWidth = a_buttonWidthSum > 0.0f ? a_buttonWidthSum : 0.0f;
		const auto requestedWidth =
			buttonWidth + spacing * static_cast<float>(a_actionCount - 1);
		const auto actionsMin = contentMax - requestedWidth;
		const auto unboundedTitleMax = actionsMin - spacing;
		const auto titleMax = unboundedTitleMax > a_contentMinX ?
			unboundedTitleMax :
			a_contentMinX;
		return {
			a_contentMinX,
			titleMax,
			actionsMin,
			contentMax,
			requestedWidth
		};
	}

	struct HostSettingsTitleRowLayout
	{
		float titleMinX{ 0.0f };
		float titleMaxX{ 0.0f };
		float actionsMinX{ 0.0f };
		float actionsMaxX{ 0.0f };
		float closeMinX{ 0.0f };
		float closeMaxX{ 0.0f };
		float reservedWidth{ 0.0f };
	};

	[[nodiscard]] constexpr HostSettingsTitleRowLayout
		ResolveHostSettingsTitleRowLayout(
			float a_contentMinX,
			float a_contentMaxX,
			float a_actionButtonWidthSum,
			size_t a_actionCount,
			float a_closeButtonWidth,
			float a_spacing) noexcept
	{
		const auto close = ResolveTrailingControlLayout(
			a_contentMinX,
			a_contentMaxX,
			a_closeButtonWidth,
			a_spacing);
		const auto actions = ResolvePageActionRowLayout(
			a_contentMinX,
			close.adjacentMaxX,
			a_actionButtonWidthSum,
			a_actionCount,
			a_spacing);
		const auto controlsMinX = a_actionCount > 0 ?
			actions.actionsMinX :
			close.controlMinX;
		return {
			a_contentMinX,
			actions.titleMaxX,
			actions.actionsMinX,
			actions.actionsMaxX,
			close.controlMinX,
			close.controlMaxX,
			close.controlMaxX - controlsMinX
		};
	}
}
