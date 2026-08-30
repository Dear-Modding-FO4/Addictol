#pragma once

#include <DearModdingUI/ThemeDefaults.h>

#include <cstdint>

namespace Addictol::DearModdingUI
{
	struct HostInterfaceSettings
	{
		Theme::IconColorMode iconColorMode{ Theme::IconColorMode::kColored };
		bool backgroundBlur{ true };

		[[nodiscard]] constexpr bool operator==(
			const HostInterfaceSettings&) const noexcept = default;
	};

	struct PersistedHostInterfaceSettings
	{
		bool monochromeIcons{ false };
		bool backgroundBlur{ true };

		[[nodiscard]] constexpr bool operator==(
			const PersistedHostInterfaceSettings&) const noexcept = default;
	};

	[[nodiscard]] constexpr HostInterfaceSettings DecodeHostInterfaceSettings(
		PersistedHostInterfaceSettings a_settings) noexcept
	{
		return {
			a_settings.monochromeIcons ?
				Theme::IconColorMode::kMonochrome :
				Theme::IconColorMode::kColored,
			a_settings.backgroundBlur
		};
	}

	[[nodiscard]] constexpr PersistedHostInterfaceSettings EncodeHostInterfaceSettings(
		HostInterfaceSettings a_settings) noexcept
	{
		return {
			a_settings.iconColorMode == Theme::IconColorMode::kMonochrome,
			a_settings.backgroundBlur
		};
	}

	enum class HostSettingsPanelEvent : uint32_t
	{
		kNone,
		kOpenRequested,
		kDismissed,
		kMenuClosed
	};

	[[nodiscard]] constexpr bool DecideHostSettingsPanelOpen(
		bool a_open,
		bool a_menuVisible,
		HostSettingsPanelEvent a_event) noexcept
	{
		if (!a_menuVisible ||
			a_event == HostSettingsPanelEvent::kDismissed ||
			a_event == HostSettingsPanelEvent::kMenuClosed)
			return false;
		if (a_event == HostSettingsPanelEvent::kOpenRequested)
			return true;
		return a_open;
	}

	struct TitleBarButtonPair
	{
		float settingsMinX{ 0.0f };
		float settingsMaxX{ 0.0f };
		float closeMinX{ 0.0f };
		float closeMaxX{ 0.0f };
	};

	[[nodiscard]] constexpr float TitleBarButtonExtent(
		float a_fontSize,
		float a_buttonPadding) noexcept
	{
		const auto fontSize = a_fontSize > 0.0f ? a_fontSize : 0.0f;
		const auto padding = a_buttonPadding > 0.0f ? a_buttonPadding : 0.0f;
		return fontSize + padding * 2.0f;
	}

	[[nodiscard]] constexpr float RightTitleBarButtonOriginX(
		float a_windowMaxX,
		float a_windowBorder,
		float a_framePaddingX,
		float a_fontSize,
		float a_offset,
		float a_buttonPadding) noexcept
	{
		return a_windowMaxX -
			a_windowBorder -
			a_framePaddingX -
			a_fontSize -
			a_offset -
			a_buttonPadding;
	}

	[[nodiscard]] constexpr TitleBarButtonPair ResolveTitleBarButtonPair(
		float a_windowMaxX,
		float a_windowBorder,
		float a_framePaddingX,
		float a_fontSize,
		float a_buttonPadding) noexcept
	{
		const auto extent = TitleBarButtonExtent(a_fontSize, a_buttonPadding);
		const auto closeMin = RightTitleBarButtonOriginX(
			a_windowMaxX,
			a_windowBorder,
			a_framePaddingX,
			a_fontSize,
			0.0f,
			a_buttonPadding);
		const auto settingsMin = RightTitleBarButtonOriginX(
			a_windowMaxX,
			a_windowBorder,
			a_framePaddingX,
			a_fontSize,
			extent,
			a_buttonPadding);
		return {
			settingsMin,
			settingsMin + extent,
			closeMin,
			closeMin + extent
		};
	}

	namespace HostSettings
	{
		[[nodiscard]] HostInterfaceSettings Current() noexcept;
		void Apply(HostInterfaceSettings a_settings) noexcept;
		void NotifyMenuVisible(bool a_visible) noexcept;
		void RequestPanelOpen(bool a_menuVisible) noexcept;
		void DismissPanel() noexcept;
		[[nodiscard]] bool IsPanelOpen() noexcept;
	}
}
