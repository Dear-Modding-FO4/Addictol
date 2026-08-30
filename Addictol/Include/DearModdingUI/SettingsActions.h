#pragma once

#include <DearModdingUI/IconGlyphs.h>

#include <array>
#include <cstdint>

namespace Addictol::DearModdingUI
{
	enum class SettingsAction : uint32_t
	{
		kReset,
		kRevert,
		kApply
	};

	inline constexpr std::array kSettingsActionOrder{
		SettingsAction::kReset,
		SettingsAction::kRevert,
		SettingsAction::kApply
	};

	[[nodiscard]] constexpr char32_t SettingsActionGlyph(
		SettingsAction a_action) noexcept
	{
		switch (a_action)
		{
		case SettingsAction::kReset:
			return PhosphorGlyph::kArrowsClockwise;
		case SettingsAction::kRevert:
			return PhosphorGlyph::kArrowCounterClockwise;
		case SettingsAction::kApply:
			return PhosphorGlyph::kFloppyDisk;
		}
		return {};
	}

	[[nodiscard]] constexpr bool SettingsActionEnabled(
		SettingsAction a_action,
		bool a_dirty) noexcept
	{
		switch (a_action)
		{
		case SettingsAction::kReset:
			return true;
		case SettingsAction::kRevert:
		case SettingsAction::kApply:
			return a_dirty;
		}
		return false;
	}

	struct SettingsActionButtonPresentation
	{
		char32_t glyph{};
		bool useTextFallback{ true };

		constexpr bool operator==(
			const SettingsActionButtonPresentation&) const noexcept = default;
	};

	[[nodiscard]] constexpr SettingsActionButtonPresentation
		ResolveSettingsActionButtonPresentation(
			SettingsAction a_action,
			bool a_glyphAvailable) noexcept
	{
		const auto glyph = SettingsActionGlyph(a_action);
		if (glyph && a_glyphAvailable)
			return { glyph, false };
		return {};
	}
}
