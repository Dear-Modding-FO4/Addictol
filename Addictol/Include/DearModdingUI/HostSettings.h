#pragma once

#include <DearModdingUI/ThemeDefaults.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI
{
	struct HostAccentColor
	{
		uint8_t red{ 0x42 };
		uint8_t green{ 0xFA };
		uint8_t blue{ 0x60 };

		[[nodiscard]] constexpr bool operator==(
			const HostAccentColor&) const noexcept = default;
	};

	inline constexpr HostAccentColor kDefaultHostAccentColor{};
	inline constexpr float kDefaultWindowBackgroundOpacity{ 0.55f };
	inline constexpr float kMinWindowBackgroundOpacity{ 0.20f };
	inline constexpr float kMaxWindowBackgroundOpacity{ 1.0f };
	inline constexpr float kDefaultBackgroundBlurStrength{ 0.30f };
	inline constexpr float kMinBackgroundBlurStrength{ 0.10f };
	inline constexpr float kMaxBackgroundBlurStrength{ 1.0f };
	inline constexpr std::string_view kDefaultBodyFontFamily{ "Jost" };

	struct HostInterfaceSettings
	{
		Theme::IconColorMode iconColorMode{ Theme::IconColorMode::kColored };
		HostAccentColor accentColor{};
		float windowBackgroundOpacity{ kDefaultWindowBackgroundOpacity };
		bool backgroundBlur{ true };
		float backgroundBlurStrength{ kDefaultBackgroundBlurStrength };
		float uiScale{ Theme::kDefaultUserScale };
		std::string bodyFontFamily{ kDefaultBodyFontFamily };

		[[nodiscard]] bool operator==(
			const HostInterfaceSettings&) const noexcept = default;
	};

	struct HostInterfacePreviewSettings
	{
		Theme::IconColorMode iconColorMode{ Theme::IconColorMode::kColored };
		HostAccentColor accentColor{};
		float windowBackgroundOpacity{ kDefaultWindowBackgroundOpacity };
		bool backgroundBlur{ true };
		float backgroundBlurStrength{ kDefaultBackgroundBlurStrength };

		[[nodiscard]] bool operator==(
			const HostInterfacePreviewSettings&) const noexcept = default;
	};

	struct PersistedHostInterfaceSettings
	{
		bool monochromeIcons{ false };
		std::string accentColor{ "#42FA60" };
		float windowBackgroundOpacity{ kDefaultWindowBackgroundOpacity };
		bool backgroundBlur{ true };
		float backgroundBlurStrength{ kDefaultBackgroundBlurStrength };
		float uiScale{ Theme::kDefaultUserScale };
		std::string bodyFontFamily{ kDefaultBodyFontFamily };

		[[nodiscard]] bool operator==(
			const PersistedHostInterfaceSettings&) const noexcept = default;
	};

	[[nodiscard]] inline float ClampHostSetting(
		float a_value,
		float a_minimum,
		float a_maximum,
		float a_default) noexcept
	{
		return std::isfinite(a_value) ?
			std::clamp(a_value, a_minimum, a_maximum) :
			a_default;
	}

	[[nodiscard]] constexpr int HexDigitValue(char a_value) noexcept
	{
		if (a_value >= '0' && a_value <= '9')
			return a_value - '0';
		if (a_value >= 'a' && a_value <= 'f')
			return a_value - 'a' + 10;
		if (a_value >= 'A' && a_value <= 'F')
			return a_value - 'A' + 10;
		return -1;
	}

	[[nodiscard]] constexpr HostAccentColor DecodeHostAccentColor(
		std::string_view a_value) noexcept
	{
		if (a_value.size() == 7 && a_value.front() == '#')
			a_value.remove_prefix(1);
		if (a_value.size() != 6)
			return kDefaultHostAccentColor;

		const auto component = [a_value](size_t a_offset) {
			const auto high = HexDigitValue(a_value[a_offset]);
			const auto low = HexDigitValue(a_value[a_offset + 1]);
			return high < 0 || low < 0 ? -1 : high * 16 + low;
		};
		const auto red = component(0);
		const auto green = component(2);
		const auto blue = component(4);
		if (red < 0 || green < 0 || blue < 0)
			return kDefaultHostAccentColor;
		return {
			static_cast<uint8_t>(red),
			static_cast<uint8_t>(green),
			static_cast<uint8_t>(blue)
		};
	}

	[[nodiscard]] inline std::string EncodeHostAccentColor(
		HostAccentColor a_color)
	{
		constexpr std::string_view digits{ "0123456789ABCDEF" };
		std::string result(7, '#');
		result[1] = digits[a_color.red >> 4];
		result[2] = digits[a_color.red & 0x0F];
		result[3] = digits[a_color.green >> 4];
		result[4] = digits[a_color.green & 0x0F];
		result[5] = digits[a_color.blue >> 4];
		result[6] = digits[a_color.blue & 0x0F];
		return result;
	}

	[[nodiscard]] constexpr ImVec4 HostAccentToImVec4(
		HostAccentColor a_color) noexcept
	{
		constexpr auto inverseByte = 1.0f / 255.0f;
		return {
			static_cast<float>(a_color.red) * inverseByte,
			static_cast<float>(a_color.green) * inverseByte,
			static_cast<float>(a_color.blue) * inverseByte,
			1.0f
		};
	}

	[[nodiscard]] inline HostAccentColor HostAccentFromImVec4(
		const ImVec4& a_color) noexcept
	{
		const auto component = [](float a_value) {
			const auto normalized = std::isfinite(a_value) ?
				std::clamp(a_value, 0.0f, 1.0f) :
				0.0f;
			return static_cast<uint8_t>(std::lround(normalized * 255.0f));
		};
		return {
			component(a_color.x),
			component(a_color.y),
			component(a_color.z)
		};
	}

	[[nodiscard]] inline std::string DecodeBodyFontFamily(
		std::string_view a_value)
	{
		if (a_value.empty() || a_value.size() > 128 ||
			a_value == "." || a_value == "..")
			return std::string{ kDefaultBodyFontFamily };
		for (const auto character : a_value)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 0x20 || character == '/' || character == '\\' ||
				character == ':' || character == '*' || character == '?' ||
				character == '"' || character == '<' || character == '>' ||
				character == '|')
				return std::string{ kDefaultBodyFontFamily };
		}
		return std::string{ a_value };
	}

	[[nodiscard]] inline HostInterfaceSettings DecodeHostInterfaceSettings(
		const PersistedHostInterfaceSettings& a_settings)
	{
		return {
			a_settings.monochromeIcons ?
				Theme::IconColorMode::kMonochrome :
				Theme::IconColorMode::kColored,
			DecodeHostAccentColor(a_settings.accentColor),
			ClampHostSetting(
				a_settings.windowBackgroundOpacity,
				kMinWindowBackgroundOpacity,
				kMaxWindowBackgroundOpacity,
				kDefaultWindowBackgroundOpacity),
			a_settings.backgroundBlur,
			ClampHostSetting(
				a_settings.backgroundBlurStrength,
				kMinBackgroundBlurStrength,
				kMaxBackgroundBlurStrength,
				kDefaultBackgroundBlurStrength),
			ClampHostSetting(
				a_settings.uiScale,
				Theme::kMinUserScale,
				Theme::kMaxUserScale,
				Theme::kDefaultUserScale),
			DecodeBodyFontFamily(a_settings.bodyFontFamily)
		};
	}

	[[nodiscard]] inline PersistedHostInterfaceSettings EncodeHostInterfaceSettings(
		const HostInterfaceSettings& a_settings)
	{
		return {
			a_settings.iconColorMode == Theme::IconColorMode::kMonochrome,
			EncodeHostAccentColor(a_settings.accentColor),
			ClampHostSetting(
				a_settings.windowBackgroundOpacity,
				kMinWindowBackgroundOpacity,
				kMaxWindowBackgroundOpacity,
				kDefaultWindowBackgroundOpacity),
			a_settings.backgroundBlur,
			ClampHostSetting(
				a_settings.backgroundBlurStrength,
				kMinBackgroundBlurStrength,
				kMaxBackgroundBlurStrength,
				kDefaultBackgroundBlurStrength),
			ClampHostSetting(
				a_settings.uiScale,
				Theme::kMinUserScale,
				Theme::kMaxUserScale,
				Theme::kDefaultUserScale),
			DecodeBodyFontFamily(a_settings.bodyFontFamily)
		};
	}

	[[nodiscard]] inline HostInterfaceSettings DefaultHostInterfaceSettings()
	{
		return {};
	}

	[[nodiscard]] constexpr HostInterfacePreviewSettings PreviewHostInterfaceSettings(
		const HostInterfaceSettings& a_settings) noexcept
	{
		return {
			a_settings.iconColorMode,
			a_settings.accentColor,
			a_settings.windowBackgroundOpacity,
			a_settings.backgroundBlur,
			a_settings.backgroundBlurStrength
		};
	}

	enum class HostSettingsPanelEvent : uint32_t
	{
		kNone,
		kToggleRequested,
		kDismissed,
		kModSelected,
		kMenuClosed
	};

	[[nodiscard]] constexpr bool DecideHostSettingsPanelOpen(
		bool a_open,
		bool a_menuVisible,
		HostSettingsPanelEvent a_event) noexcept
	{
		if (!a_menuVisible ||
			a_event == HostSettingsPanelEvent::kDismissed ||
			a_event == HostSettingsPanelEvent::kModSelected ||
			a_event == HostSettingsPanelEvent::kMenuClosed)
			return false;
		if (a_event == HostSettingsPanelEvent::kToggleRequested)
			return !a_open;
		return a_open;
	}

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

	namespace HostSettings
	{
		[[nodiscard]] HostInterfaceSettings Current() noexcept;
		[[nodiscard]] HostInterfacePreviewSettings EffectivePreview() noexcept;
		void Apply(HostInterfaceSettings a_settings) noexcept;
		void SetPreview(
			HostInterfacePreviewSettings a_settings,
			uint64_t a_panelRevision) noexcept;
		void NotifyMenuVisible(bool a_visible) noexcept;
		void TogglePanel(bool a_menuVisible) noexcept;
		void NotifyModSelected() noexcept;
		void DismissPanel() noexcept;
		[[nodiscard]] bool IsPanelOpen() noexcept;
		[[nodiscard]] uint64_t PanelRevision() noexcept;
	}
}
