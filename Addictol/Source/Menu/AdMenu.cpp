#include <Platform/AdImguiTheme.h>
#include <DearModdingUI/Host.h>
#include <Menu/AdMenu.h>
#include <Platform/AdPlatformImgui.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenuLogControl.h>

#include <REX/REX.h>
#include <resource_version2.h>

#include <Windows.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <atomic>
#include <mutex>

#undef ERROR

namespace Addictol
{
	static REX::TOML::Str<> sAdditionalMenuToggleKey{ "Additional"sv, "sMenuToggleKey"sv, "F11" };
	static REX::TOML::U32<> uAdditionalMenuRefreshMs{ "Additional"sv, "uMenuRefreshMs"sv, 250 };

	namespace menuDetail
	{
		inline constexpr ImVec2 kWindowSize{ 1100.0f, 700.0f };

		static std::atomic<bool> s_requested{ false };
		static std::atomic<uint64_t> s_openGeneration{ 0 };
		static std::atomic<uint32_t> s_toggleKey{ kMenuDefaultToggleKey };
		static std::atomic<uint32_t> s_refreshMs{ kMenuMinRefreshMs };
		static std::atomic<bool> s_backendFailureLogged{ false };
		static DMUI_ClientHandle s_client{ DMUI_INVALID_CLIENT_HANDLE };

		static uint64_t s_qpcFrequency{ 0 };
		static double s_lastDrawMs{ 0.0 };
		static float s_dpiScale{ 1.0f };
		static ImVec2 s_lastWindowPosition{};
		static ImVec2 s_lastWindowSize{};
		static bool s_geometryObserved{ false };
		static bool s_geometryDirty{ false };

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
			s_dpiScale = a_window ? ImGui_ImplWin32_GetDpiScaleForHwnd(a_window) : 1.0f;
			if (s_dpiScale <= 0.0f)
				s_dpiScale = 1.0f;
			Theme::Apply(io, ImGui::GetStyle(), s_dpiScale);
			REX::INFO("Menu: ImGui configured at {:.2f}x DPI scale"sv, s_dpiScale);
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

		void SaveWindowGeometry() noexcept
		{
			const auto iniPath = PlatformImgui::GetConfigurePath();
			if (!ImGui::GetCurrentContext() || iniPath.empty())
				return;

			ImGui::SaveIniSettingsToDisk(iniPath.c_str());
		}

		void DrawWindow() noexcept
		{
			ImGui::SetNextWindowSize(
				ImVec2(kWindowSize.x * s_dpiScale, kWindowSize.y * s_dpiScale),
				ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);

			auto open = true;
			const auto visible = ImGui::Begin("Dear Modding UI", &open);
			const auto position = ImGui::GetWindowPos();
			const auto size = ImGui::GetWindowSize();
			const auto geometryChanged = s_geometryObserved &&
				(position.x != s_lastWindowPosition.x || position.y != s_lastWindowPosition.y ||
					size.x != s_lastWindowSize.x || size.y != s_lastWindowSize.y);
			if (geometryChanged)
				s_geometryDirty = true;
			s_geometryObserved = true;
			s_lastWindowPosition = position;
			s_lastWindowSize = size;

			if (visible && ImGui::BeginTabBar("addictol_menu_tabs", ImGuiTabBarFlags_None))
			{
				const auto selected = DearModdingUI::SelectedPage();
				for (const auto& page : DearModdingUI::OrderedPages())
				{
					if (page.kind != DMUI_PAGE_KIND_SETTINGS ||
						page.callbackFailed)
						continue;
					const auto flags = page.handle == selected ?
						ImGuiTabItemFlags_SetSelected :
						ImGuiTabItemFlags_None;
					if (ImGui::BeginTabItem(page.imguiLabel.c_str(), nullptr, flags))
					{
						DearModdingUI::DrawPage(page.handle);
						ImGui::EndTabItem();
					}
					if (page.handle == selected)
						DearModdingUI::ClearPageSelection(page.handle);
				}
				ImGui::EndTabBar();
			}
			ImGui::End();

			if (s_geometryDirty && !geometryChanged && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				SaveWindowGeometry();
				s_geometryDirty = false;
			}
			if (!open)
			{
				(void)DearModdingUI::SetMenuVisible(false);
				SaveWindowGeometry();
				PlatformImgui::SetDrawingEnabled(false);
			}
		}

		void DrawSink() noexcept
		{
			const auto start = ReadQpc();
			DearModdingUI::DrawDemandedOverlays();
			if (DearModdingUI::IsMenuVisible())
				DrawWindow();
			s_lastDrawMs = QpcToMilliseconds(ReadQpc() - start, s_qpcFrequency);
		}

		void ToggleSink(uint32_t a_virtualKey) noexcept
		{
			const auto decision = DecideMenuToggle(
				a_virtualKey,
				s_toggleKey.load(std::memory_order_relaxed),
				DearModdingUI::IsMenuVisible(),
				PlatformImgui::IsReady());
			if (!decision.matched)
				return;

			const auto result = DearModdingUI::SetMenuVisible(decision.open);
			if (result == DMUI_RESULT_OK && decision.open)
				s_openGeneration.fetch_add(1, std::memory_order_acq_rel);
			PlatformImgui::SetDrawingEnabled(result == DMUI_RESULT_OK && decision.open);

			if (decision.open && result != DMUI_RESULT_OK)
			{
				if (!s_backendFailureLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Menu: DearModdingUI cannot open, result {}."sv, result);
			}
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
			nullptr
		};
		const auto registered = DearModdingUI::RegisterInternalClient(
			&descriptor, &s_client);
		if (registered != DMUI_RESULT_OK)
		{
			REX::ERROR("Menu: Addictol DearModdingUI client registration failed, result {}."sv,
				registered);
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
