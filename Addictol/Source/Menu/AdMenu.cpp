#include <Platform/AdImguiTheme.h>
#include <Menu/AdMenu.h>
#include <Platform/AdPlatformImgui.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenuLogControl.h>

#include <REX/REX.h>

#include <Windows.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <atomic>

#undef ERROR

namespace Addictol
{
	static REX::TOML::Str<> sAdditionalMenuToggleKey{ "Additional"sv, "sMenuToggleKey"sv, "F11" };
	static REX::TOML::U32<> uAdditionalMenuRefreshMs{ "Additional"sv, "uMenuRefreshMs"sv, 250 };

	namespace menuDetail
	{
		inline constexpr ImVec2 kWindowSize{ 1100.0f, 700.0f };

		static MenuPanelTable<REX::TOML::Bool<>> s_panels{};
		static std::atomic<bool> s_requested{ false };
		static std::atomic<bool> s_open{ false };
		static std::atomic<uint64_t> s_openGeneration{ 0 };
		static std::atomic<uint32_t> s_toggleKey{ kMenuDefaultToggleKey };
		static std::atomic<uint32_t> s_refreshMs{ kMenuMinRefreshMs };
		static std::atomic<bool> s_backendFailureLogged{ false };

		// render thread only
		static uint64_t s_qpcFrequency{ 0 };
		static double s_lastDrawMs{ 0.0 };
		static float s_dpiScale{ 1.0f };
		static ImVec2 s_lastWindowPosition{};
		static ImVec2 s_lastWindowSize{};
		static bool s_geometryObserved{ false };
		static bool s_geometryDirty{ false };

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
			const auto visible = ImGui::Begin("Addictol", &open);
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
				DrawPanels(
					s_panels,
					[](const char* a_name) noexcept { return ImGui::BeginTabItem(a_name); },
					[]() noexcept { ImGui::EndTabItem(); });
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
				s_open.store(false, std::memory_order_release);
				SaveWindowGeometry();
				PlatformImgui::SetDrawingEnabled(false);
			}
		}

		void DrawSink() noexcept
		{
			if (!s_open.load(std::memory_order_acquire))
				return;

			const auto start = ReadQpc();
			DrawWindow();
			s_lastDrawMs = QpcToMilliseconds(ReadQpc() - start, s_qpcFrequency);
		}

		// window thread; platform serializes access
		void ToggleSink(uint32_t a_virtualKey) noexcept
		{
			const auto decision = DecideMenuToggle(
				a_virtualKey,
				s_toggleKey.load(std::memory_order_relaxed),
				s_open.load(std::memory_order_acquire),
				PlatformImgui::IsDrawingEnabled());
			if (!decision.matched)
				return;

			if (decision.open)
				s_openGeneration.fetch_add(1, std::memory_order_acq_rel);
			s_open.store(decision.open, std::memory_order_release);
			PlatformImgui::SetDrawingEnabled(decision.open);

			if (decision.open && !PlatformImgui::IsDrawingEnabled())
			{
				s_open.store(false, std::memory_order_release);
				if (!s_backendFailureLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Menu: ImGui is not drawable; the menu cannot open."sv);
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	bool Menu::RegisterPanel(const Panel& a_panel) noexcept
	{
		using namespace menuDetail;

		const auto result = s_panels.Add(a_panel.name, a_panel.draw, a_panel.gate, a_panel.context);
		if (result != Registration::kAccepted)
			REX::WARN("Menu: panel \"{}\" rejected, {}."sv, a_panel.name, Describe(result));
		return result == Registration::kAccepted;
	}

	bool Menu::Install() noexcept
	{
		using namespace menuDetail;

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
		s_requested.store(true, std::memory_order_release);

		REX::INFO("Menu: toggle key {} with a {} ms refresh; the menu starts closed."sv,
			MenuToggleKeyName(parsed.virtualKey),
			refreshMs);
		return true;
	}

	void Menu::FinalizeRegistration() noexcept
	{
		using namespace menuDetail;

		const auto result = FinalizeMenuPanels(
			s_panels,
			kMenuLogControlPanelName,
			&DrawMenuLogControlPanel,
			s_requested.load(std::memory_order_acquire),
			[]() noexcept {
				if (!PlatformImgui::RegisterSetupSink("Menu"sv, &SetupSink) ||
					!PlatformImgui::RegisterDrawSink("Menu"sv, &DrawSink) ||
					!PlatformImgui::RegisterToggleSink("Menu"sv, &ToggleSink))
					REX::ERROR("Menu: the ImGui platform refused a required sink; the window cannot open."sv);
			});

		if (result != Registration::kAccepted)
			REX::ERROR(
				"Menu: Log Control panel could not be registered; the menu will not install ({})."sv,
				Describe(result));
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
