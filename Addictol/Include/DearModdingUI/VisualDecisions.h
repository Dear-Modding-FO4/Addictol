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
		bool a_modalVisible,
		[[maybe_unused]] bool a_overlayDemanded,
		bool a_nativeGameCursorVisible,
		bool a_customCursorEnabled,
		bool a_activeCustomCursorLoaded) noexcept
	{
		if (!a_modalVisible)
			return {};
		const auto custom =
			!a_nativeGameCursorVisible &&
			a_customCursorEnabled &&
			a_activeCustomCursorLoaded;
		return {
			true,
			true,
			!a_nativeGameCursorVisible && !custom,
			custom
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
}
