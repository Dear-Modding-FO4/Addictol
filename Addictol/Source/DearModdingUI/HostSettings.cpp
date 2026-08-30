#include <DearModdingUI/HostSettings.h>

#include <REX/REX.h>

#include <atomic>
#include <mutex>
#include <optional>

namespace Addictol::DearModdingUI::HostSettings
{
	using namespace std::literals;

	namespace
	{
		REX::TOML::Bool<> bAdditionalMenuMonochromeIcons{
			"Additional"sv, "bMenuMonochromeIcons"sv, false
		};
		REX::TOML::Str<> sAdditionalMenuAccentColor{
			"Additional"sv, "sMenuAccentColor"sv, "#42FA60"
		};
		REX::TOML::F32<> fAdditionalMenuWindowOpacity{
			"Additional"sv, "fMenuWindowOpacity"sv, kDefaultWindowBackgroundOpacity
		};
		REX::TOML::Bool<> bAdditionalMenuBackgroundBlur{
			"Additional"sv, "bMenuBackgroundBlur"sv, true
		};
		REX::TOML::F32<> fAdditionalMenuBackgroundBlurStrength{
			"Additional"sv, "fMenuBackgroundBlurStrength"sv,
			kDefaultBackgroundBlurStrength
		};
		REX::TOML::F32<> fAdditionalMenuUiScale{
			"Additional"sv, "fMenuUiScale"sv, Theme::kDefaultUserScale
		};
		REX::TOML::Str<> sAdditionalMenuBodyFontFamily{
			"Additional"sv, "sMenuBodyFontFamily"sv,
			std::string{ kDefaultBodyFontFamily }
		};
		std::atomic<bool> g_panelOpen{ false };
		std::atomic<uint64_t> g_panelRevision{ 0 };
		std::mutex g_previewMutex;
		std::optional<HostInterfacePreviewSettings> g_preview;

		void StorePanelEvent(
			bool a_menuVisible,
			HostSettingsPanelEvent a_event) noexcept
		{
			const auto current = g_panelOpen.load(std::memory_order_acquire);
			const auto next = DecideHostSettingsPanelOpen(
				current,
				a_menuVisible,
				a_event);
			const std::scoped_lock lock{ g_previewMutex };
			if (current && !next)
			{
				g_preview.reset();
				g_panelRevision.fetch_add(1, std::memory_order_release);
			}
			g_panelOpen.store(next, std::memory_order_release);
		}
	}

	HostInterfaceSettings Current() noexcept
	{
		return DecodeHostInterfaceSettings({
			bAdditionalMenuMonochromeIcons.GetValue(),
			sAdditionalMenuAccentColor.GetValue(),
			fAdditionalMenuWindowOpacity.GetValue(),
			bAdditionalMenuBackgroundBlur.GetValue(),
			fAdditionalMenuBackgroundBlurStrength.GetValue(),
			fAdditionalMenuUiScale.GetValue(),
			sAdditionalMenuBodyFontFamily.GetValue()
		});
	}

	HostInterfacePreviewSettings EffectivePreview() noexcept
	{
		{
			const std::scoped_lock lock{ g_previewMutex };
			if (g_preview)
				return *g_preview;
		}
		return PreviewHostInterfaceSettings(Current());
	}

	void Apply(HostInterfaceSettings a_settings) noexcept
	{
		a_settings = DecodeHostInterfaceSettings(
			EncodeHostInterfaceSettings(a_settings));
		if (a_settings == Current())
			return;

		const auto persisted = EncodeHostInterfaceSettings(a_settings);
		bAdditionalMenuMonochromeIcons.SetValue(persisted.monochromeIcons);
		sAdditionalMenuAccentColor.SetValue(persisted.accentColor);
		fAdditionalMenuWindowOpacity.SetValue(
			persisted.windowBackgroundOpacity);
		bAdditionalMenuBackgroundBlur.SetValue(persisted.backgroundBlur);
		fAdditionalMenuBackgroundBlurStrength.SetValue(
			persisted.backgroundBlurStrength);
		fAdditionalMenuUiScale.SetValue(persisted.uiScale);
		sAdditionalMenuBodyFontFamily.SetValue(persisted.bodyFontFamily);
		try
		{
			REX::FTomlSettingStore::GetSingleton()->Save();
		}
		catch (...)
		{
			REX::WARN("DearModdingUI: interface settings could not be persisted"sv);
		}
	}

	void SetPreview(
		HostInterfacePreviewSettings a_settings,
		uint64_t a_panelRevision) noexcept
	{
		const std::scoped_lock lock{ g_previewMutex };
		if (!g_panelOpen.load(std::memory_order_acquire) ||
			g_panelRevision.load(std::memory_order_acquire) !=
				a_panelRevision)
			return;
		g_preview = a_settings;
	}

	void NotifyMenuVisible(bool a_visible) noexcept
	{
		StorePanelEvent(
			a_visible,
			a_visible ?
				HostSettingsPanelEvent::kNone :
				HostSettingsPanelEvent::kMenuClosed);
	}

	void TogglePanel(bool a_menuVisible) noexcept
	{
		StorePanelEvent(
			a_menuVisible,
			HostSettingsPanelEvent::kToggleRequested);
	}

	void NotifyModSelected() noexcept
	{
		StorePanelEvent(true, HostSettingsPanelEvent::kModSelected);
	}

	void DismissPanel() noexcept
	{
		StorePanelEvent(true, HostSettingsPanelEvent::kDismissed);
	}

	bool IsPanelOpen() noexcept
	{
		return g_panelOpen.load(std::memory_order_acquire);
	}

	uint64_t PanelRevision() noexcept
	{
		return g_panelRevision.load(std::memory_order_acquire);
	}
}
