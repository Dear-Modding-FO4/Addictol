#include <Core/AdLogControl.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuLogControl.h>
#include <Menu/AdMenuFormatting.h>

#include <Windows.h>

namespace Addictol
{
	namespace
	{
		using namespace MenuUi;
		using Menu::ReportPresentationResult;

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

		void Refresh() noexcept
		{
			const auto frequency = GetQpcFrequency();
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
			static const auto choices = [] {
				std::array<dmui::ChoiceOption<LogControl::Level>, kMenuLogLevels.size()> result;
				for (size_t index = 0; index < kMenuLogLevels.size(); ++index)
				{
					const auto level = kMenuLogLevels[index];
					const std::string name{ LogControl::LevelName(level) };
					result[index] = { level, name, name };
				}
				return result;
			}();
			const auto selected = dmui::DrawChoice<LogControl::Level>(
				a_label, a_current, choices, "unknown", a_label);
			if (selected.changed)
			{
				a_setter(*selected.selected);
				a_current = *selected.selected;
			}
		}
	}

	void DrawMenuLogControlPanel(void*) noexcept
	{
		Refresh();

		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), "Log control", { .fontRole = DMUI_FONT_ROLE_TITLE }));
		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), "Overrides apply to this session only; they reset when the game exits.",
			Menu::kMutedText));
		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), "[Additional] sLogLevel and sLogFlushLevel are the persistent TOML controls.",
			Menu::kMutedText));
		dmui::ui::Separator();

		ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "Levels", Menu::kHeadingText));
		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), "Record level decides which lines are kept at all.", Menu::kMutedText));
		DrawLevelCombo("Record level", s_cache.level, &LogControl::SetLevel);
		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), "Flush level forces a synchronous disk write at that level or higher.",
			Menu::kMutedText));
		DrawLevelCombo("Flush level", s_cache.flushLevel, &LogControl::SetFlushLevel);

		dmui::ui::Spacing();
		ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "Output", Menu::kHeadingText));
		ReportPresentationResult(dmui::DrawLabeledValue(
			Menu::Client(), "Recent output", FormatLinesInLastMinute(s_cache.stats.linesPerMinute),
			{ .valueStyle = Menu::kBodyText }));
		ReportPresentationResult(dmui::DrawLabeledValue(
			Menu::Client(), "Lines written (session)", FormatCount(s_cache.stats.written),
			{ .valueStyle = Menu::kBodyText }));
		ReportPresentationResult(dmui::DrawLabeledValue(
			Menu::Client(), "Flushes (session)", FormatCount(s_cache.stats.flushed),
			{ .valueStyle = Menu::kBodyText }));

		dmui::ui::Separator();
		ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), Print(
			"refresh %.3f ms, cadence %u ms",
			QpcToMilliseconds(s_cache.refreshTicks, GetQpcFrequency()),
			Menu::RefreshMs()), Menu::kMutedText));
	}
}
