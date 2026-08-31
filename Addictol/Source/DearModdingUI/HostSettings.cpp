#include <DearModdingUI/HostSettings.h>

#include <Core/Settings/AdSettingPersistence.h>
#include <DearModdingUI/Host.h>

#include <REX/REX.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace Addictol::DearModdingUI::HostSettings
{
	using namespace std::literals;

	namespace
	{






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

	bool Apply(HostInterfaceSettings a_settings) noexcept
	{
		a_settings = DecodeHostInterfaceSettings(
			EncodeHostInterfaceSettings(a_settings));
		if (a_settings == Current())
		{
			(void)SetHostStatus(
				DMUI_STATUS_SEVERITY_SUCCESS,
				"Settings saved.");
			return true;
		}

		const auto persisted = EncodeHostInterfaceSettings(a_settings);
		auto settings = SettingsRepository::GetSingleton().Snapshot();
		const auto set = [&](std::string_view a_key, SettingValue a_value) {
			const auto* entry =
				SettingRegistry::GetSingleton().Find("Additional", a_key);
			const auto position = std::ranges::find(
				settings,
				entry,
				&SettingValueSnapshot::setting);
			if (position != settings.end())
				position->value = std::move(a_value);
		};
		set("bMenuMonochromeIcons", persisted.monochromeIcons);
		set("sMenuAccentColor", persisted.accentColor);
		set("fMenuWindowOpacity",
			static_cast<double>(persisted.windowBackgroundOpacity));
		set("bMenuBackgroundBlur", persisted.backgroundBlur);
		set("fMenuBackgroundBlurStrength",
			static_cast<double>(persisted.backgroundBlurStrength));
		set("fMenuUiScale", static_cast<double>(persisted.uiScale));
		set("sMenuBodyFontFamily", persisted.bodyFontFamily);
		const auto result = SettingsRepository::GetSingleton().Apply(settings);
		if (!result.success)
		{
			REX::WARN(
				"DearModdingUI: interface settings could not be persisted: {}"sv,
				result.error);
			(void)SetHostStatus(
				DMUI_STATUS_SEVERITY_ERROR,
				result.error);
			return false;
		}
		(void)SetHostStatus(
			DMUI_STATUS_SEVERITY_SUCCESS,
			"Settings saved.");
		return true;
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
