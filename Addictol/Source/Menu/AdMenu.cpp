#include <Menu/AdMenu.h>
#include <Menu/AdMenuHome.h>
#include <Menu/AdMenuLogControl.h>
#include <Menu/AdMenuModules.h>
#include <Menu/AdMenuSettings.h>

#include <Core/AdUtils.h>
#include <Telemetry/AdTelemetryHub.h>

#include <REX/REX.h>
#include <resource_version2.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace Addictol
{
	namespace menuDetail
	{
		static dmui::Client s_client{
			"dear-modding.addictol",
			"Addictol",
			dmui::Version{ VERSION_MAJOR, VERSION_MINOR },
			dmui::kForwardingClient
		};
		static std::atomic<uint32_t> s_refreshMs{ kMenuMinRefreshMs };
		static std::atomic<bool> s_connected{ false };
		static std::vector<Menu::Panel> s_pendingPanels;
		static DMUI_ThemeColors s_themeColors{ sizeof(DMUI_ThemeColors) };

		void Configure() noexcept
		{
			const auto refreshMs = ClampMenuRefreshMs(uAdditionalMenuRefreshMs.GetValue());
			if (refreshMs != uAdditionalMenuRefreshMs.GetValue())
			{
				REX::WARN(
					"Menu: uMenuRefreshMs {} is outside {}-{} ms; using {} ms."sv,
					uAdditionalMenuRefreshMs.GetValue(),
					kMenuMinRefreshMs,
					kMenuMaxRefreshMs,
					refreshMs);
			}
			s_refreshMs.store(refreshMs, std::memory_order_relaxed);
			REX::INFO("Menu: client refresh cadence is {} ms."sv, refreshMs);
		}

		void RefreshThemeColors() noexcept
		{
			if (const auto colors = s_client.GetThemeColors())
				s_themeColors = *colors;
		}

		void DrawPanel(Menu::Panel a_panel) noexcept
		{
			RefreshThemeColors();
			a_panel.draw(a_panel.context);
		}

		[[nodiscard]] bool RegisterPage(const Menu::Panel& a_panel) noexcept
		{
			const auto page = s_client.AddPage(
				a_panel.id,
				a_panel.name,
				a_panel.category,
				[a_panel] {
					DrawPanel(a_panel);
				},
				a_panel.summary,
				a_panel.sortKey);
			if (!page)
			{
				REX::WARN(
					"Menu: page \"{}\" rejected, result {}."sv,
					a_panel.name,
					DMUI_ResultToString(s_client.LastResult()));
			}
			return page.has_value();
		}
	}

	bool Menu::RegisterPanel(const Panel& a_panel) noexcept
	{
		using namespace menuDetail;

		if (a_panel.gate && !a_panel.gate->GetValue())
			return true;
		if (s_connected.load(std::memory_order_acquire))
			return RegisterPage(a_panel);
		try
		{
			s_pendingPanels.push_back(a_panel);
			return true;
		}
		catch (...)
		{
			REX::ERROR("Menu: could not retain page \"{}\" for registration."sv, a_panel.name);
			return false;
		}
	}

	dmui::Client& Menu::Client() noexcept
	{
		return menuDetail::s_client;
	}

	const DMUI_ThemeColors& Menu::ThemeColors() noexcept
	{
		return menuDetail::s_themeColors;
	}

	void Menu::ReportStatus(
		DMUI_StatusSeverity a_severity,
		const char* a_message) noexcept
	{
		using namespace menuDetail;

		if (!s_connected.load(std::memory_order_acquire))
			return;
		if (!s_client.SetStatus(a_severity, a_message))
		{
			REX::WARN(
				"Menu: status report rejected, result {}."sv,
				DMUI_ResultToString(s_client.LastResult()));
		}
	}

	bool Menu::Install() noexcept
	{
		using namespace menuDetail;

		Configure();
		if (!s_client.Connect())
		{
			if (!s_client.HostPresent())
			{
				REX::INFO(
					"Menu: DearModdingUI.dll is not loaded; Addictol continues without an in-game menu."sv);
				return true;
			}
			REX::ERROR(
				"Menu: DearModdingUI client connection failed, result {}."sv,
				DMUI_ResultToString(s_client.LastResult()));
			return false;
		}
		if (!ImGui::IsForwardVersionCompatible())
		{
			REX::WARN(
				"Menu: DearModdingUI ImGui version {} does not match forwarding header version {}; drawing may be incorrect."sv,
				ImGui::GetHostImGuiVersionNum(),
				ImGui::kForwardImGuiVersionNum);
		}

		Menu::BeginSettingsPageFrame();
		if (!s_client.AddFrameObserver([] {
				Menu::EndSettingsPageFrame(s_client.IsMenuVisible().value_or(false));
				Menu::BeginSettingsPageFrame();
			}))
		{
			REX::ERROR(
				"Menu: frame lifecycle registration failed, result {}."sv,
				DMUI_ResultToString(s_client.LastResult()));
			return false;
		}

		if (!s_client.AddAction(
				"copy-diagnostics",
				"Copy diagnostics",
				"clipboard-text",
				"Copy the game runtime and Addictol module outcomes to the clipboard.",
				[] {
					CopyDiagnosticsSummaryToClipboard(nullptr);
				}))
		{
			REX::ERROR(
				"Menu: diagnostics action registration failed, result {}."sv,
				DMUI_ResultToString(s_client.LastResult()));
			return false;
		}
		if (!Telemetry::ConnectDearModdingUI(s_client))
		{
			REX::ERROR(
				"Menu: DearModdingUI telemetry registration failed, result {}."sv,
				DMUI_ResultToString(s_client.LastResult()));
			return false;
		}

		if (!RegisterPage({
				"home",
				"Home",
				"Addictol",
				"Overview, live module status, project links, and FAQ.",
				0,
				&DrawHomePage,
				nullptr,
				nullptr
			}) ||
			!RegisterPage({
				"settings",
				"Settings",
				"Addictol",
				"Configure Addictol fixes, performance, visuals, gameplay, and diagnostics.",
				100,
				&DrawSettingsPage,
				nullptr,
				nullptr
			}))
			return false;

		s_connected.store(true, std::memory_order_release);
		for (const auto& panel : s_pendingPanels)
		{
			if (!RegisterPage(panel))
				return false;
		}
		s_pendingPanels.clear();

		REX::INFO("Menu: Addictol connected to DearModdingUI."sv);
		return true;
	}

	void Menu::FinalizeRegistration() noexcept
	{
		if (!RegisterPanel({
				"modules",
				"Modules",
				"Addictol",
				"Individual install, disable, skip, and failure outcomes for every module.",
				200,
				&DrawModulesPage,
				nullptr,
				nullptr
			}))
			REX::ERROR("Menu: Modules page could not be retained."sv);

		if (!RegisterPanel({
				"log-control",
				kMenuLogControlPanelName.data(),
				"Diagnostics",
				"Runtime logging levels and output statistics.",
				1000,
				&DrawMenuLogControlPanel,
				nullptr,
				nullptr
			}))
			REX::ERROR("Menu: Log Control page could not be retained."sv);
	}

	uint32_t Menu::RefreshMs() noexcept
	{
		return menuDetail::s_refreshMs.load(std::memory_order_relaxed);
	}
}
