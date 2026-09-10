#include <Core/AdClock.h>
#include <Menu/AdMenu.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Menu/AdMenuTelemetry.h>
#include <Menu/AdMenuFormatting.h>

#include <DearModdingUI/UI.h>

#include <array>
#include <cfloat>

namespace Addictol
{
	namespace
	{
		using namespace MenuUi;
		using Menu::ReportPresentationResult;

		struct TelemetryMenuCache
		{
			TelemetrySnapshot current{};
			TelemetrySnapshot previous{};
			TelemetrySnapshot candidate{};
			TelemetryStats stats{};
			std::array<MetricValue, 120> history{};
			std::array<float, 120> frameTimes{};
			std::array<FrameRecord, 16> frameRecords{};
			size_t frameTimeCount{ 0 };
			size_t frameRecordCount{ 0 };
			uint64_t refreshedAtQpc{ 0 };
			uint64_t refreshTicks{ 0 };
			bool attempted{ false };
			bool hasData{ false };
		};

		TelemetryMenuCache s_cache;

		[[nodiscard]] size_t FindColumn(
			std::span<const MetricDescriptor> a_columns,
			std::string_view a_key) noexcept
		{
			for (size_t index = 0; index < a_columns.size(); ++index)
			{
				if (a_columns[index].key == a_key)
					return index;
			}
			return a_columns.size();
		}

		void RefreshCache() noexcept
		{
			const auto frequency = Addictol::GetQpcFrequency();
			const auto now = Addictol::ReadQpc();
			if (!ShouldRefreshPanel(
					s_cache.attempted,
					now,
					s_cache.refreshedAtQpc,
					frequency,
					Menu::RefreshMs()))
				return;

			const auto start = Addictol::ReadQpc();
			const auto& hub = Telemetry::Hub();
			if (hub.CopyLatest(s_cache.candidate) &&
				(!s_cache.hasData ||
					s_cache.candidate.sequence != s_cache.current.sequence))
			{
				std::swap(s_cache.previous, s_cache.current);
				std::swap(s_cache.current, s_cache.candidate);
				s_cache.hasData = true;

				const auto columns = hub.Columns();
				const auto frameColumn = FindColumn(columns, "frame.mean_ms"sv);
				s_cache.frameTimeCount = 0;
				if (frameColumn < columns.size())
				{
					const auto count =
						hub.CopyMetricHistory(frameColumn, s_cache.history);
					for (size_t index = 0; index < count; ++index)
					{
						if (s_cache.history[index].valid)
						{
							s_cache.frameTimes[s_cache.frameTimeCount++] =
								static_cast<float>(s_cache.history[index].value);
						}
					}
				}
			}
			s_cache.stats = hub.Stats();
			s_cache.frameRecordCount = hub.CopyFrameRecords(s_cache.frameRecords);

			const auto finish = Addictol::ReadQpc();
			s_cache.refreshedAtQpc = finish;
			s_cache.refreshTicks = finish > start ? finish - start : 0;
			s_cache.attempted = true;
		}

		[[nodiscard]] MetricValue IntervalDelta(
			size_t a_index,
			std::string_view a_key) noexcept
		{
			if (!IsCumulativeTelemetryMetric(a_key) ||
				!s_cache.previous.sequence ||
				s_cache.previous.sequence + 1 != s_cache.current.sequence ||
				a_index >= s_cache.previous.values.size() ||
				a_index >= s_cache.current.values.size())
				return {};

			const auto current = s_cache.current.values[a_index];
			const auto previous = s_cache.previous.values[a_index];
			if (!current.valid || !previous.valid || current.value < previous.value)
				return {};
			return { current.value - previous.value, true };
		}

		void DrawDisplay(const TelemetryValueDisplay& a_display) noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(
				Menu::Client(), a_display.Text(),
				a_display.valid ? Menu::kBodyText : Menu::kMutedText));
		}

		void DrawMetricRow(
			size_t a_index,
			std::string_view a_label,
			std::span<const MetricDescriptor> a_columns) noexcept
		{
			const auto& descriptor = a_columns[a_index];
			const auto value = a_index < s_cache.current.values.size() ?
				s_cache.current.values[a_index] : MetricValue{};
			const auto display = FormatTelemetryValue(value, descriptor.unit);

			dmui::ui::TableNextRow();
			(void)dmui::ui::TableNextColumn();
			dmui::ui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
			(void)dmui::ui::TableNextColumn();
			DrawDisplay(display);
			(void)dmui::ui::TableNextColumn();
			if (display.progress)
			{
				const auto color = display.fraction >= 0.9f ?
					dmui::ToUIVec4(Menu::ThemeColors().error) :
					display.fraction >= 0.75f ?
						dmui::ToUIVec4(Menu::ThemeColors().warning) :
						dmui::ToUIVec4(Menu::ThemeColors().accentMuted);
				dmui::ui::PushStyleColor(dmui::ui::Color::kPlotHistogram, color);
				dmui::ui::ProgressBar(display.fraction, dmui::ui::Vec2(-FLT_MIN, 0.0f), "");
				dmui::ui::PopStyleColor();
			}
			else if (IsCumulativeTelemetryMetric(descriptor.key))
			{
				const auto delta = IntervalDelta(a_index, descriptor.key);
				if (delta.valid)
					ReportPresentationResult(dmui::DrawStyledText(
						Menu::Client(), Print("+%.0f", delta.value), Menu::kBodyText));
				else
					ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "-", Menu::kMutedText));
			}
		}

		void DrawFpsRow(std::span<const MetricDescriptor> a_columns) noexcept
		{
			const auto index = FindColumn(a_columns, "frame.count"sv);
			const auto valid = index < a_columns.size() &&
				index < s_cache.current.values.size() &&
				s_cache.current.values[index].valid &&
				s_cache.current.intervalMs > 0.0;

			dmui::ui::TableNextRow();
			(void)dmui::ui::TableNextColumn();
			dmui::ui::TextUnformatted("frame.fps");
			(void)dmui::ui::TableNextColumn();
			if (valid)
			{
				ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), Print(
					"%.2f fps",
					s_cache.current.values[index].value * 1000.0 /
						s_cache.current.intervalMs), Menu::kBodyText));
			}
			else
				ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "-", Menu::kMutedText));
			(void)dmui::ui::TableNextColumn();
		}

		template<class Label>
		void DrawMetricTable(const char* a_id, Label&& a_label, bool a_fps = false) noexcept
		{
			constexpr auto flags =
				dmui::ui::TableFlags::kResizable | dmui::ui::TableFlags::kRowBg |
				dmui::ui::TableFlags::kBordersInner | dmui::ui::TableFlags::kBordersOuter;
			if (!dmui::ui::BeginTable(a_id, 3, flags))
				return;

			dmui::ui::TableSetupColumn("Metric", dmui::ui::TableColumnFlags::kWidthStretch, 0.52f);
			dmui::ui::TableSetupColumn("Value", dmui::ui::TableColumnFlags::kWidthFixed, 150.0f);
			dmui::ui::TableSetupColumn("Change / usage", dmui::ui::TableColumnFlags::kWidthStretch, 0.30f);
			dmui::ui::TableHeadersRow();
			const auto columns = Telemetry::Hub().Columns();
			if (a_fps)
				DrawFpsRow(columns);
			for (size_t index = 0; index < columns.size(); ++index)
			{
				const auto label = a_label(columns[index].key);
				if (!label.empty())
					DrawMetricRow(index, label, columns);
			}
			dmui::ui::EndTable();
		}

		void DrawOverviewMetrics() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "Key metrics", Menu::kHeadingText));
			DrawMetricTable(
				"TelemetryOverviewMetrics",
				[](std::string_view a_key) noexcept {
					for (const auto key : kTelemetryOverviewMetrics)
					{
						if (a_key == key)
							return key;
					}
					return ""sv;
				},
				true);
		}

		void DrawMetricGroup(const TelemetryMetricGroup& a_group) noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), a_group.heading, Menu::kHeadingText));
			DrawMetricTable(
				a_group.prefix.data(),
				[&a_group](std::string_view a_key) noexcept {
					return a_key.starts_with(a_group.prefix) ?
						TelemetryMetricLabel(a_key) : ""sv;
				});
		}

		void DrawFrameHistory() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "Frame time history", Menu::kHeadingText));
			if (!s_cache.frameTimeCount)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), "No valid frame-time samples are available.", Menu::kMutedText));
				return;
			}
			dmui::ui::PlotLines(
				"##TelemetryFrameTime",
				s_cache.frameTimes.data(),
				static_cast<int>(s_cache.frameTimeCount),
				0,
				"Mean frame time (ms)",
				FLT_MAX,
				FLT_MAX,
				dmui::ui::Vec2(0.0f, 100.0f));
		}

		void DrawFrameRecords() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(
				Menu::Client(), "Recent frame records", Menu::kHeadingText));
			if (!s_cache.frameRecordCount)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), "No frame has crossed the recording threshold.", Menu::kMutedText));
				return;
			}
			const auto style = Menu::StyleMetrics();
			if (!style)
				return;
			const auto tableHeight =
				dmui::ui::GetTextLineHeightWithSpacing() *
					static_cast<float>(s_cache.frameRecords.size() + 1) +
				style->cellPadding.y * 2.0f;
			if (!dmui::ui::BeginTable(
					"TelemetryFrameRecords",
					2,
					Menu::kDiagnosticTableFlags,
					dmui::ui::Vec2(0.0f, tableHeight)))
				return;
			dmui::ui::TableSetupColumn("QPC time", dmui::ui::TableColumnFlags::kWidthStretch);
			dmui::ui::TableSetupColumn("Frame time", dmui::ui::TableColumnFlags::kWidthFixed);
			dmui::ui::TableHeadersRow();
			const auto frequency = Addictol::GetQpcFrequency();
			for (size_t index = 0; index < s_cache.frameRecordCount; ++index)
			{
				const auto& record = s_cache.frameRecords[index];
				const auto seconds = frequency ?
					static_cast<double>(record.qpc) / static_cast<double>(frequency) : 0.0;
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableNextColumn();
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), Print("%.3f s", seconds), Menu::kBodyText));
				(void)dmui::ui::TableNextColumn();
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), FormatMs(static_cast<double>(record.durationUs) / 1000.0),
					Menu::kBodyText));
			}
			dmui::ui::EndTable();
		}

		void DrawSeries() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), "Zlib series", Menu::kHeadingText));
			if (!dmui::ui::BeginTable("TelemetrySeries", 5, Menu::kDiagnosticTableFlags, dmui::ui::Vec2(0.0f, 260.0f)))
				return;
			dmui::ui::TableSetupColumn("Series", dmui::ui::TableColumnFlags::kWidthStretch);
			dmui::ui::TableSetupColumn("Bucket", dmui::ui::TableColumnFlags::kWidthStretch);
			dmui::ui::TableSetupColumn("Calls", dmui::ui::TableColumnFlags::kWidthFixed);
			dmui::ui::TableSetupColumn("Ticks", dmui::ui::TableColumnFlags::kWidthFixed);
			dmui::ui::TableSetupColumn("Bytes", dmui::ui::TableColumnFlags::kWidthFixed);
			dmui::ui::TableHeadersRow();
			for (const auto& sample : s_cache.current.series)
			{
				if (!sample.calls)
					continue;
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableNextColumn();
				dmui::ui::TextUnformatted(
					sample.series.data(), sample.series.data() + sample.series.size());
				(void)dmui::ui::TableNextColumn();
				dmui::ui::TextUnformatted(
					sample.bucket.data(), sample.bucket.data() + sample.bucket.size());
				(void)dmui::ui::TableNextColumn();
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), FormatCount(sample.calls), Menu::kBodyText));
				(void)dmui::ui::TableNextColumn();
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), FormatCount(sample.ticks), Menu::kBodyText));
				(void)dmui::ui::TableNextColumn();
				ReportPresentationResult(dmui::DrawStyledText(
					Menu::Client(), FormatBytes(sample.bytes), Menu::kBodyText));
			}
			dmui::ui::EndTable();
		}

		void DrawOverviewStatus() noexcept
		{
			dmui::ui::Text(
				"Sample %llu  interval %.3f ms  late %.3f ms",
				static_cast<unsigned long long>(s_cache.current.sequence),
				s_cache.current.intervalMs,
				s_cache.current.latenessMs);
			dmui::ui::Text(
				"Ring overwrites %llu  skipped %llu  frame-record overflows %llu",
				static_cast<unsigned long long>(s_cache.stats.overwrittenSamples),
				static_cast<unsigned long long>(s_cache.stats.skippedSamples),
				static_cast<unsigned long long>(s_cache.stats.frameRecordOverflows));
		}

		void DrawPanelFooter() noexcept
		{
			dmui::ui::Separator();
			ReportPresentationResult(dmui::DrawStyledText(Menu::Client(), Print(
				"refresh %.3f ms, cadence %u ms",
				QpcToMilliseconds(s_cache.refreshTicks, Addictol::GetQpcFrequency()),
				Menu::RefreshMs()), Menu::kMutedText));
		}

	}

	void DrawMenuTelemetryPanel(void* a_context) noexcept
	{
		const auto& panel = *static_cast<const TelemetryPanelDefinition*>(a_context);
		RefreshCache();

		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), panel.page.displayName, { .fontRole = DMUI_FONT_ROLE_TITLE }));
		ReportPresentationResult(dmui::DrawStyledText(
			Menu::Client(), panel.page.summary, Menu::kMutedText));
		dmui::ui::Separator();
		if (!s_cache.hasData)
		{
			ReportPresentationResult(dmui::DrawStyledText(
				Menu::Client(), "Waiting for the first sample.", Menu::kMutedText));
			DrawPanelFooter();
			return;
		}

		if (panel.panel == TelemetryPanel::kOverview)
		{
			DrawOverviewStatus();
			dmui::ui::Spacing();
			DrawFrameHistory();
			dmui::ui::Spacing();
			DrawOverviewMetrics();
			dmui::ui::Spacing();
			DrawFrameRecords();
		}
		else
		{
			for (const auto& group : kTelemetryMetricGroups)
			{
				if (group.panel != panel.panel)
					continue;
				DrawMetricGroup(group);
				dmui::ui::Spacing();
			}
			if (panel.panel == TelemetryPanel::kDecompression)
				DrawSeries();
		}
		DrawPanelFooter();
	}
}
