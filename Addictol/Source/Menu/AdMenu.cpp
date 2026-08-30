#include <DearModdingUI/Theme.h>
#include <DearModdingUI/CursorLoader.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/Shell.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuHome.h>
#include <Platform/AdPlatformImgui.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenuLogControl.h>

#include <REX/REX.h>
#include <resource_version2.h>

#include <Windows.h>

#include <imgui/imgui.h>

#include <atomic>
#include <mutex>

#undef ERROR

namespace Addictol
{
	static REX::TOML::Str<> sAdditionalMenuToggleKey{ "Additional"sv, "sMenuToggleKey"sv, "F11" };
	static REX::TOML::U32<> uAdditionalMenuRefreshMs{ "Additional"sv, "uMenuRefreshMs"sv, 250 };

	namespace menuDetail
	{
		static std::atomic<bool> s_requested{ false };
		static std::atomic<uint64_t> s_openGeneration{ 0 };
		static std::atomic<uint32_t> s_toggleKey{ kMenuDefaultToggleKey };
		static std::atomic<uint32_t> s_refreshMs{ kMenuMinRefreshMs };
		static std::atomic<bool> s_backendFailureLogged{ false };
		static DMUI_ClientHandle s_client{ DMUI_INVALID_CLIENT_HANDLE };

		static uint64_t s_qpcFrequency{ 0 };
		static double s_lastDrawMs{ 0.0 };

		void Configure() noexcept
		{
			static std::once_flag once;
			std::call_once(once, [] {
				const auto configured = sAdditionalMenuToggleKey.GetValue();
				const auto parsed = ParseMenuToggleKey(configured);
				if (!parsed.recognized)
				{
					REX::WARN(
						"Menu: sMenuToggleKey \"{}\" is not one of F1-F12, Home, End, Insert, or Delete; falling back to F11."sv,
						configured);
				}
				s_toggleKey.store(parsed.virtualKey, std::memory_order_relaxed);

				const auto refreshMs = ClampMenuRefreshMs(uAdditionalMenuRefreshMs.GetValue());
				if (refreshMs != uAdditionalMenuRefreshMs.GetValue())
				{
					REX::WARN("Menu: uMenuRefreshMs {} is outside {}-{} ms; using {} ms."sv,
						uAdditionalMenuRefreshMs.GetValue(),
						kMenuMinRefreshMs,
						kMenuMaxRefreshMs,
						refreshMs);
				}
				s_refreshMs.store(refreshMs, std::memory_order_relaxed);
				REX::INFO("Menu: common toggle key {} with a {} ms refresh."sv,
					MenuToggleKeyName(parsed.virtualKey),
					refreshMs);
			});
		}

		[[nodiscard]] uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER counter{};
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		[[nodiscard]] uint64_t ReadQpcFrequency() noexcept
		{
			LARGE_INTEGER frequency{};
			if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
				return 0;
			return static_cast<uint64_t>(frequency.QuadPart);
		}

		void SetupSink(void* a_window) noexcept
		{
			auto& io = ImGui::GetIO();
			s_qpcFrequency = ReadQpcFrequency();
			DearModdingUI::Theme::Initialize(a_window);
			DearModdingUI::CursorLoader::Initialize(a_window);
			REX::INFO("Menu: DearModdingUI visuals configured"sv);
		}

		void DMUI_CALL OnHostReady(
			const DMUI_HostReadyInfo* a_info,
			[[maybe_unused]] void* a_userData) noexcept
		{
			if (!a_info || a_info->structSize < sizeof(DMUI_HostReadyInfo))
				return;
			ImGui::SetCurrentContext(static_cast<ImGuiContext*>(a_info->imguiContext));
			ImGui::SetAllocatorFunctions(
				a_info->imguiAlloc,
				a_info->imguiFree,
				a_info->imguiAllocatorUserData);
			REX::INFO("Menu: DearModdingUI host is ready"sv);
		}

		void DMUI_CALL OnHostUnavailable(
			DMUI_UnavailableReason a_reason,
			[[maybe_unused]] void* a_userData) noexcept
		{
			REX::ERROR("Menu: DearModdingUI host unavailable, reason {}"sv, a_reason);
		}

		void DrawSink() noexcept
		{
			const auto start = ReadQpc();
			DearModdingUI::DrawDemandedOverlays();
			if (DearModdingUI::IsMenuVisible())
				DearModdingUI::DrawShell();
			if (!DearModdingUI::IsMenuVisible())
				PlatformImgui::SetDrawingEnabled(false);
			s_lastDrawMs = QpcToMilliseconds(ReadQpc() - start, s_qpcFrequency);
		}

		[[nodiscard]] bool ToggleSink(uint32_t a_virtualKey) noexcept
		{
			const auto decision = DecideMenuToggle(
				a_virtualKey,
				s_toggleKey.load(std::memory_order_relaxed),
				DearModdingUI::IsMenuVisible(),
				PlatformImgui::IsReady());
			if (!decision.matched)
				return false;

			const auto result = DearModdingUI::SetMenuVisible(decision.open);
			if (result == DMUI_RESULT_OK && decision.open)
				s_openGeneration.fetch_add(1, std::memory_order_acq_rel);
			PlatformImgui::SetDrawingEnabled(result == DMUI_RESULT_OK && decision.open);

			if (decision.open && result != DMUI_RESULT_OK)
			{
				if (!s_backendFailureLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Menu: DearModdingUI cannot open, result {}."sv, result);
			}
			return true;
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	bool Menu::RegisterPanel(const Panel& a_panel) noexcept
	{
		using namespace menuDetail;

		if (!s_requested.load(std::memory_order_acquire) ||
			(a_panel.gate && !a_panel.gate->GetValue()))
			return true;
		const DMUI_PageDescriptor descriptor{
			sizeof(DMUI_PageDescriptor),
			a_panel.id,
			a_panel.name,
			a_panel.category,
			a_panel.summary,
			a_panel.sortKey,
			DMUI_PAGE_KIND_SETTINGS,
			a_panel.draw,
			a_panel.context
		};
		DMUI_PageHandle page{ DMUI_INVALID_PAGE_HANDLE };
		const auto result = DearModdingUI::HostAPI().registerPage(
			s_client, &descriptor, &page);
		if (result != DMUI_RESULT_OK)
			REX::WARN("Menu: page \"{}\" rejected, result {}."sv, a_panel.name, result);
		return result == DMUI_RESULT_OK;
	}

	bool Menu::Install() noexcept
	{
		using namespace menuDetail;

		Configure();

		const DMUI_ClientDescriptor descriptor{
			sizeof(DMUI_ClientDescriptor),
			DMUI_API_VERSION_CURRENT,
			"dear-modding.addictol",
			"Addictol",
			DMUI_MAKE_VERSION(VERSION_MAJOR, VERSION_MINOR),
			&DearModdingUI::HostFingerprint(),
			&OnHostReady,
			&OnHostUnavailable,
			nullptr,
			DMUI_CLIENT_CAPABILITY_NONE
		};
		const auto registered = DearModdingUI::RegisterInternalClient(
			&descriptor, &s_client);
		if (registered != DMUI_RESULT_OK)
		{
			REX::ERROR("Menu: Addictol DearModdingUI client registration failed, result {}."sv,
				registered);
			return false;
		}

		const DMUI_ActionDescriptor copyDiagnostics{
			sizeof(DMUI_ActionDescriptor),
			"copy-diagnostics",
			"Copy diagnostics",
			"clipboard-text",
			"Copy the game runtime and Addictol module outcomes to the clipboard.",
			0,
			&CopyDiagnosticsSummaryToClipboard,
			nullptr
		};
		DMUI_ActionHandle copyDiagnosticsAction{ DMUI_INVALID_ACTION_HANDLE };
		const auto actionResult = DearModdingUI::HostAPI().registerAction(
			s_client, &copyDiagnostics, &copyDiagnosticsAction);
		if (actionResult != DMUI_RESULT_OK)
		{
			REX::ERROR(
				"Menu: Addictol diagnostics action registration failed, result {}."sv,
				actionResult);
			return false;
		}

		const DMUI_PageDescriptor home{
			sizeof(DMUI_PageDescriptor),
			"home",
			"Home",
			"Addictol",
			"Overview, live module status, project links, and FAQ.",
			0,
			DMUI_PAGE_KIND_SETTINGS,
			&DrawHomePage,
			nullptr
		};
		DMUI_PageHandle homePage{ DMUI_INVALID_PAGE_HANDLE };
		const auto homeResult = DearModdingUI::HostAPI().registerPage(
			s_client, &home, &homePage);
		if (homeResult != DMUI_RESULT_OK)
		{
			REX::ERROR(
				"Menu: Addictol home page registration failed, result {}."sv,
				homeResult);
			return false;
		}
		s_requested.store(true, std::memory_order_release);

		REX::INFO("Menu: Addictol pages enabled; the common menu starts closed."sv);
		return true;
	}

	void Menu::FinalizeRegistration() noexcept
	{
		using namespace menuDetail;

		Configure();
		if (s_requested.load(std::memory_order_acquire) &&
			!RegisterPanel({
				"log-control",
				kMenuLogControlPanelName.data(),
				"Diagnostics",
				"Runtime logging levels and output statistics.",
				1000,
				&DrawMenuLogControlPanel,
				nullptr,
				nullptr
			}))
			REX::ERROR("Menu: Log Control page could not be registered."sv);

		if (!PlatformImgui::RegisterSetupSink("DearModdingUI"sv, &SetupSink) ||
			!PlatformImgui::RegisterDrawSink("DearModdingUI"sv, &DrawSink) ||
			!PlatformImgui::RegisterToggleSink("DearModdingUI"sv, &ToggleSink))
		{
			REX::ERROR("Menu: the ImGui platform refused DearModdingUI host sinks."sv);
			DearModdingUI::DeferBackendUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED);
		}
	}

	uint32_t Menu::RefreshMs() noexcept
	{
		return menuDetail::s_refreshMs.load(std::memory_order_relaxed);
	}

	std::string_view Menu::ToggleKeyName() noexcept
	{
		return MenuToggleKeyName(menuDetail::s_toggleKey.load(std::memory_order_relaxed));
	}

	double Menu::LastDrawMs() noexcept
	{
		return menuDetail::s_lastDrawMs;
	}

	uint64_t Menu::OpenGeneration() noexcept
	{
		return menuDetail::s_openGeneration.load(std::memory_order_acquire);
	}
}
