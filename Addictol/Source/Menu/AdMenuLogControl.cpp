#include <Core/AdLogControl.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuLogControl.h>
#include <Menu/AdMenuWidgets.h>

#include <Windows.h>

namespace Addictol
{
	namespace
	{
		using namespace MenuUi;

		struct LogControlCache
		{
			LogControl::Level level{ LogControl::Level::kInfo };
			LogControl::Level flushLevel{ LogControl::Level::kInfo };
			LogControl::Stats stats;
			uint64_t refreshedAtQpc{ 0 };
			uint64_t refreshTicks{ 0 };
			bool hasData{ false };
		};

		LogControlCache s_cache;
		uint64_t s_qpcFrequency{ 0 };

		[[nodiscard]] uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER counter{};
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		[[nodiscard]] uint64_t QpcFrequency() noexcept
		{
			if (!s_qpcFrequency)
			{
				LARGE_INTEGER frequency{};
				QueryPerformanceFrequency(&frequency);
				s_qpcFrequency = static_cast<uint64_t>(frequency.QuadPart);
			}
			return s_qpcFrequency;
		}

		void Refresh() noexcept
		{
			const auto frequency = QpcFrequency();
			const auto now = ReadQpc();
			if (!ShouldRefreshPanel(
					s_cache.hasData,
					now,
					s_cache.refreshedAtQpc,
					frequency,
					Menu::RefreshMs()))
				return;

			const auto start = ReadQpc();
			s_cache.level = LogControl::GetLevel();
			s_cache.flushLevel = LogControl::GetFlushLevel();
			s_cache.stats = LogControl::CopyStats();
			const auto finish = ReadQpc();

			s_cache.refreshedAtQpc = finish;
			s_cache.refreshTicks = finish > start ? finish - start : 0;
			s_cache.hasData = true;
		}

		void DrawLevelCombo(
			const char* a_label,
			LogControl::Level& a_current,
			void (*a_setter)(LogControl::Level) noexcept) noexcept
		{
			auto preview = "unknown"sv;
			for (const auto level : kMenuLogLevels)
			{
				if (level == a_current)
				{
					preview = LogControl::LevelName(level);
					break;
				}
			}
			if (!ImGui::BeginCombo(a_label, preview.data()))
				return;

			for (const auto level : kMenuLogLevels)
			{
				const auto name = LogControl::LevelName(level);
				const auto selected = level == a_current;
				if (ImGui::Selectable(name.data(), selected))
				{
					a_setter(level);
					a_current = level;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	void DrawMenuLogControlPanel(const void*) noexcept
	{
		Refresh();

		Title("Log control"sv);
		Muted("Overrides apply to this session only; they reset when the game exits."sv);
		Muted("[Additional] sLogLevel and sLogFlushLevel are the persistent TOML controls."sv);
		ImGui::Separator();

		Heading("Levels"sv);
		Muted("Record level decides which lines are kept at all."sv);
		DrawLevelCombo("Record level", s_cache.level, &LogControl::SetLevel);
		Muted("Flush level forces a synchronous disk write at that level or higher."sv);
		DrawLevelCombo("Flush level", s_cache.flushLevel, &LogControl::SetFlushLevel);

		ImGui::Spacing();
		Heading("Output"sv);
		LabeledValue("Recent output"sv, FormatLinesInLastMinute(s_cache.stats.linesPerMinute));
		LabeledValue("Lines written (session)"sv, FormatCount(s_cache.stats.written));
		LabeledValue("Flushes (session)"sv, FormatCount(s_cache.stats.flushed));

		ImGui::Separator();
		Muted(Print(
			"refresh %.3f ms, cadence %u ms",
			QpcToMilliseconds(s_cache.refreshTicks, QpcFrequency()),
			Menu::RefreshMs()));
	}
}
