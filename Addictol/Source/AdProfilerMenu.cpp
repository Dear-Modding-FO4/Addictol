#include <AdImguiTheme.h>
#include <AdPlatformImgui.h>
#include <AdProfilerAllocator.h>
#include <AdProfilerMenu.h>
#include <AdProfilerMenuModel.h>
#include <AdUtils.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

#include <REX/REX.h>

#include <Windows.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <atomic>
#include <array>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>

#undef ERROR

namespace Addictol
{
	static REX::TOML::Str<> sProfilerMenuToggleKey{ "Profiler"sv, "sProfilerMenuToggleKey"sv, "F11" };
	static REX::TOML::U32<> uProfilerMenuRefreshMs{ "Profiler"sv, "uProfilerMenuRefreshMs"sv, 250 };

	namespace profilerMenuDetail
	{
		inline constexpr ImVec2 kWindowSize{ 1100.0f, 700.0f };

		static std::atomic<bool> s_open{ false };
		static std::atomic<uint32_t> s_openGeneration{ 0 };
		static std::atomic<uint32_t> s_toggleKey{ kProfilerMenuDefaultToggleKey };
		static std::atomic<uint32_t> s_refreshMs{ kProfilerMenuMinRefreshMs };
		static std::atomic<bool> s_backendFailureLogged{ false };

		// Render-thread state; no other thread reads it.
		static ProfilerMenuModel* s_model{ nullptr };
		static uint32_t s_seenGeneration{ 0 };
		static uint64_t s_qpcFrequency{ 0 };
		static double s_lastDrawMs{ 0.0 };
		static float s_dpiScale{ 1.0f };
		static ImVec2 s_lastWindowPosition{};
		static ImVec2 s_lastWindowSize{};
		static bool s_geometryObserved{ false };
		static bool s_geometryDirty{ false };

		struct PanelEntry
		{
			ProfilerMenuTab tab;
			ProfilerMenuPanelDraw draw;
		};

		inline constexpr std::array kPanels{
			PanelEntry{ ProfilerMenuTab::kOverview, &DrawProfilerMenuOverview },
			PanelEntry{ ProfilerMenuTab::kFrameHitch, &DrawProfilerMenuFrameHitch },
			PanelEntry{ ProfilerMenuTab::kDecompression, &DrawProfilerMenuDecompression },
			PanelEntry{ ProfilerMenuTab::kAllocator, &DrawProfilerMenuAllocator },
			PanelEntry{ ProfilerMenuTab::kMemory, &DrawProfilerMenuMemory },
			PanelEntry{ ProfilerMenuTab::kModules, &DrawProfilerMenuModules },
			PanelEntry{ ProfilerMenuTab::kTextureDecode, &DrawProfilerMenuTextureDecode }
		};

		static void SetupSink(void* a_window) noexcept
		{
			ProfilerAllocator::SamplingScope sampling;
			auto& io = ImGui::GetIO();

			s_dpiScale = a_window ? ImGui_ImplWin32_GetDpiScaleForHwnd(a_window) : 1.0f;
			if (s_dpiScale <= 0.0f)
				s_dpiScale = 1.0f;
			Theme::Apply(io, ImGui::GetStyle(), s_dpiScale);
			REX::INFO("Profiler menu: ImGui configured at {:.2f}x DPI scale"sv, s_dpiScale);
		}

		[[nodiscard]] static ProfilerMenuModel* EnsureModel() noexcept
		{
			if (s_model)
				return s_model;

			// Caches are allocated on the first open and kept for the process lifetime.
			s_model = new (std::nothrow) ProfilerMenuModel;
			if (!s_model)
			{
				REX::ERROR("Profiler menu: the view caches could not be allocated; the menu stays closed."sv);
				return nullptr;
			}

			s_model->Reserve();
			s_qpcFrequency = GetQpcFrequency();
			return s_model;
		}

		static void ClearTransientState(ProfilerMenuModel& a_model) noexcept
		{
			a_model.MutableModules().filter = {};
		}

		static void SaveWindowGeometry() noexcept
		{
			auto iniPath = PlatformImgui::GetConfigurePath();
			if (!ImGui::GetCurrentContext() || iniPath.empty())
				return;

			const ProfilerAllocator::SamplingScope sampling;
			ImGui::SaveIniSettingsToDisk(iniPath.c_str());
		}

		static void DrawWindow(ProfilerMenuModel& a_model) noexcept
		{
			ProfilerMenuDrawContext context;
			context.nowQpc = ReadQpc();
			context.qpcFrequency = s_qpcFrequency;
			context.refreshMs = s_refreshMs.load(std::memory_order_relaxed);
			context.toggleKey = s_toggleKey.load(std::memory_order_relaxed);
			context.lastDrawMs = s_lastDrawMs;

			ImGui::SetNextWindowSize(
				ImVec2(kWindowSize.x * s_dpiScale, kWindowSize.y * s_dpiScale),
				ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);

			auto open = true;
			const auto visible = ImGui::Begin("Addictol Profiler", &open);
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

			if (visible &&
				ImGui::BeginTabBar("addictol_profiler_tabs", ImGuiTabBarFlags_None))
			{
				for (const auto& panel : kPanels)
				{
					const auto name = Describe(panel.tab);
					const auto active = ImGui::BeginTabItem(name.data());
					a_model.RefreshPanel(
						panel.tab,
						active,
						context.nowQpc,
						s_qpcFrequency,
						context.refreshMs);
					if (!active)
						continue;

					context.activeTab = panel.tab;
					panel.draw(a_model, context);
					ImGui::EndTabItem();
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
				s_open.store(false, std::memory_order_release);
				SaveWindowGeometry();
				PlatformImgui::SetDrawingEnabled(false);
			}
		}

		static void DrawSink() noexcept
		{
			// A closed menu costs one atomic load: no timing, no snapshot, no allocation.
			if (!s_open.load(std::memory_order_acquire))
				return;

			// The viewer must not appear in the allocator counters it displays.
			const ProfilerAllocator::SamplingScope sampling;
			const auto start = ReadQpc();
			auto* model = EnsureModel();
			if (!model)
			{
				s_open.store(false, std::memory_order_release);
				PlatformImgui::SetDrawingEnabled(false);
				return;
			}

			const auto generation = s_openGeneration.load(std::memory_order_acquire);
			if (generation != s_seenGeneration)
			{
				s_seenGeneration = generation;
				ClearTransientState(*model);
			}

			DrawWindow(*model);
			s_lastDrawMs = QpcToMilliseconds(ReadQpc() - start, s_qpcFrequency);
		}

		// Runs on the window thread; the platform serializes context and input transitions.
		static void ToggleSink(uint32_t a_virtualKey) noexcept
		{
			if (a_virtualKey != s_toggleKey.load(std::memory_order_relaxed))
				return;

			// The platform disables drawing when a frame fails, so the menu follows it instead of desyncing.
			const auto current = s_open.load(std::memory_order_acquire) &&
				PlatformImgui::IsDrawingEnabled();
			const auto open = !current;
			if (open)
				s_openGeneration.fetch_add(1, std::memory_order_acq_rel);
			s_open.store(open, std::memory_order_release);
			PlatformImgui::SetDrawingEnabled(open);

			// A failed backend refuses to draw, so the menu closes instead of waiting for a frame.
			if (open && !PlatformImgui::IsDrawingEnabled())
			{
				s_open.store(false, std::memory_order_release);
				if (!s_backendFailureLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("Profiler menu: ImGui is not drawable; the menu cannot open."sv);
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	bool ProfilerMenu::Install() noexcept
	{
		using namespace profilerMenuDetail;

		const auto configured = sProfilerMenuToggleKey.GetValue();
		const auto parsed = ParseProfilerMenuToggleKey(configured);
		if (!parsed.recognized)
		{
			REX::WARN(
				"Profiler menu: sProfilerMenuToggleKey \"{}\" is not one of F1-F12, Home, End, Insert, or Delete; falling back to F11."sv,
				configured);
		}
		s_toggleKey.store(parsed.virtualKey, std::memory_order_relaxed);

		const auto refreshMs = ClampProfilerMenuRefreshMs(uProfilerMenuRefreshMs.GetValue());
		if (refreshMs != uProfilerMenuRefreshMs.GetValue())
		{
			REX::WARN("Profiler menu: uProfilerMenuRefreshMs {} is outside {}-{} ms; using {} ms."sv,
				uProfilerMenuRefreshMs.GetValue(),
				kProfilerMenuMinRefreshMs,
				kProfilerMenuMaxRefreshMs,
				refreshMs);
		}
		s_refreshMs.store(refreshMs, std::memory_order_relaxed);

		if (!PlatformImgui::RegisterSetupSink("Profiler Menu"sv, &SetupSink) ||
			!PlatformImgui::RegisterDrawSink("Profiler Menu"sv, &DrawSink) ||
			!PlatformImgui::RegisterToggleSink("Profiler Menu"sv, &ToggleSink))
			return false;

		REX::INFO("Profiler menu: toggle key {} with a {} ms refresh; the menu starts closed."sv,
			ProfilerMenuToggleKeyName(parsed.virtualKey),
			refreshMs);
		return true;
	}
}
