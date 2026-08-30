#include <DearModdingUI/HostSettings.h>

#include <REX/REX.h>

#include <atomic>

namespace Addictol::DearModdingUI::HostSettings
{
	using namespace std::literals;

	namespace
	{
		REX::TOML::Bool<> bAdditionalMenuMonochromeIcons{
			"Additional"sv, "bMenuMonochromeIcons"sv, false
		};
		REX::TOML::Bool<> bAdditionalMenuBackgroundBlur{
			"Additional"sv, "bMenuBackgroundBlur"sv, true
		};
		std::atomic<bool> g_panelOpen{ false };
	}

	HostInterfaceSettings Current() noexcept
	{
		return DecodeHostInterfaceSettings({
			bAdditionalMenuMonochromeIcons.GetValue(),
			bAdditionalMenuBackgroundBlur.GetValue()
		});
	}

	void Apply(HostInterfaceSettings a_settings) noexcept
	{
		if (a_settings == Current())
			return;

		const auto persisted = EncodeHostInterfaceSettings(a_settings);
		bAdditionalMenuMonochromeIcons.SetValue(persisted.monochromeIcons);
		bAdditionalMenuBackgroundBlur.SetValue(persisted.backgroundBlur);
		try
		{
			REX::FTomlSettingStore::GetSingleton()->Save();
		}
		catch (...)
		{
			REX::WARN("DearModdingUI: interface settings could not be persisted"sv);
		}
	}

	void NotifyMenuVisible(bool a_visible) noexcept
	{
		const auto current = g_panelOpen.load(std::memory_order_acquire);
		g_panelOpen.store(
			DecideHostSettingsPanelOpen(
				current,
				a_visible,
				a_visible ?
					HostSettingsPanelEvent::kNone :
					HostSettingsPanelEvent::kMenuClosed),
			std::memory_order_release);
	}

	void RequestPanelOpen(bool a_menuVisible) noexcept
	{
		const auto current = g_panelOpen.load(std::memory_order_acquire);
		g_panelOpen.store(
			DecideHostSettingsPanelOpen(
				current,
				a_menuVisible,
				HostSettingsPanelEvent::kOpenRequested),
			std::memory_order_release);
	}

	void DismissPanel() noexcept
	{
		const auto current = g_panelOpen.load(std::memory_order_acquire);
		g_panelOpen.store(
			DecideHostSettingsPanelOpen(
				current,
				true,
				HostSettingsPanelEvent::kDismissed),
			std::memory_order_release);
	}

	bool IsPanelOpen() noexcept
	{
		return g_panelOpen.load(std::memory_order_acquire);
	}
}
