#include <Core/AdClock.h>
#include <DearModdingUI/Theme.h>
#include <Menu/AdMenu.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Menu/AdMenuTelemetry.h>
#include <Menu/AdMenuWidgets.h>

#include <imgui/imgui.h>

#include <array>
#include <cfloat>

namespace Addictol
{
	namespace
	{
		using namespace MenuUi;

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
			if (a_display.valid)
				MonoCell(a_display.Text());
			else
				Muted(a_display.Text());
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

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
			ImGui::TableNextColumn();
			DrawDisplay(display);
			ImGui::TableNextColumn();
			if (display.progress)
			{
				const auto color = display.fraction >= 0.9f ? Theme::colors::kError :
					display.fraction >= 0.75f ? Theme::colors::kWarning :
					Theme::colors::AccentMuted();
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
				ImGui::ProgressBar(display.fraction, ImVec2(-FLT_MIN, 0.0f), "");
				ImGui::PopStyleColor();
			}
			else if (IsCumulativeTelemetryMetric(descriptor.key))
			{
				const auto delta = IntervalDelta(a_index, descriptor.key);
				if (delta.valid)
					MonoCell(Print("+%.0f", delta.value));
				else
					Muted("-"sv);
			}
		}

		void DrawFpsRow(std::span<const MetricDescriptor> a_columns) noexcept
		{
			const auto index = FindColumn(a_columns, "frame.count"sv);
			const auto valid = index < a_columns.size() &&
				index < s_cache.current.values.size() &&
				s_cache.current.values[index].valid &&
				s_cache.current.intervalMs > 0.0;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("frame.fps");
			ImGui::TableNextColumn();
			if (valid)
			{
				MonoCell(Print(
					"%.2f fps",
					s_cache.current.values[index].value * 1000.0 /
						s_cache.current.intervalMs));
			}
			else
				Muted("-"sv);
			ImGui::TableNextColumn();
		}

		template<class Label>
		void DrawMetricTable(const char* a_id, Label&& a_label, bool a_fps = false) noexcept
		{
			constexpr auto flags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter;
			if (!ImGui::BeginTable(a_id, 3, flags))
				return;

			ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 0.52f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Change / usage", ImGuiTableColumnFlags_WidthStretch, 0.30f);
			ImGui::TableHeadersRow();
			const auto columns = Telemetry::Hub().Columns();
			if (a_fps)
				DrawFpsRow(columns);
			for (size_t index = 0; index < columns.size(); ++index)
			{
				const auto label = a_label(columns[index].key);
				if (!label.empty())
					DrawMetricRow(index, label, columns);
			}
			ImGui::EndTable();
		}

		void DrawOverviewMetrics() noexcept
		{
			Heading("Key metrics"sv);
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
			Heading(a_group.heading);
			DrawMetricTable(
				a_group.prefix.data(),
				[&a_group](std::string_view a_key) noexcept {
					return a_key.starts_with(a_group.prefix) ?
						TelemetryMetricLabel(a_key) : ""sv;
				});
		}

		void DrawFrameHistory() noexcept
		{
			Heading("Frame time history"sv);
			if (!s_cache.frameTimeCount)
			{
				Muted("No valid frame-time samples are available."sv);
				return;
			}
			ImGui::PlotLines(
				"##TelemetryFrameTime",
				s_cache.frameTimes.data(),
				static_cast<int>(s_cache.frameTimeCount),
				0,
				"Mean frame time (ms)",
				FLT_MAX,
				FLT_MAX,
				ImVec2(0.0f, 100.0f));
		}

		void DrawFrameRecords() noexcept
		{
			Heading("Recent frame records"sv);
			if (!s_cache.frameRecordCount)
			{
				Muted("No frame has crossed the recording threshold."sv);
				return;
			}
			const auto tableHeight =
				ImGui::GetTextLineHeightWithSpacing() *
					static_cast<float>(s_cache.frameRecords.size() + 1) +
				ImGui::GetStyle().CellPadding.y * 2.0f;
			if (!ImGui::BeginTable(
					"TelemetryFrameRecords",
					2,
					kTableFlags,
					ImVec2(0.0f, tableHeight)))
				return;
			ImGui::TableSetupColumn("QPC time", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Frame time", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			const auto frequency = Addictol::GetQpcFrequency();
			for (size_t index = 0; index < s_cache.frameRecordCount; ++index)
			{
				const auto& record = s_cache.frameRecords[index];
				const auto seconds = frequency ?
					static_cast<double>(record.qpc) / static_cast<double>(frequency) : 0.0;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				MonoCell(Print("%.3f s", seconds));
				ImGui::TableNextColumn();
				MonoCell(FormatMs(static_cast<double>(record.durationUs) / 1000.0));
			}
			ImGui::EndTable();
		}

		void DrawSeries() noexcept
		{
			Heading("Zlib series"sv);
			if (!ImGui::BeginTable("TelemetrySeries", 5, kTableFlags, ImVec2(0.0f, 260.0f)))
				return;
			ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (const auto& sample : s_cache.current.series)
			{
				if (!sample.calls)
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(
					sample.series.data(), sample.series.data() + sample.series.size());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(
					sample.bucket.data(), sample.bucket.data() + sample.bucket.size());
				ImGui::TableNextColumn();
				MonoCell(FormatCount(sample.calls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(sample.ticks));
				ImGui::TableNextColumn();
				MonoCell(FormatBytes(sample.bytes));
			}
			ImGui::EndTable();
		}

		void DrawOverviewStatus() noexcept
		{
			ImGui::Text(
				"Sample %llu  interval %.3f ms  late %.3f ms",
				static_cast<unsigned long long>(s_cache.current.sequence),
				s_cache.current.intervalMs,
				s_cache.current.latenessMs);
			ImGui::Text(
				"Ring overwrites %llu  skipped %llu  frame-record overflows %llu",
				static_cast<unsigned long long>(s_cache.stats.overwrittenSamples),
				static_cast<unsigned long long>(s_cache.stats.skippedSamples),
				static_cast<unsigned long long>(s_cache.stats.frameRecordOverflows));
		}

		void DrawPanelFooter() noexcept
		{
			ImGui::Separator();
			Muted(Print(
				"refresh %.3f ms, cadence %u ms",
				QpcToMilliseconds(s_cache.refreshTicks, Addictol::GetQpcFrequency()),
				Menu::RefreshMs()));
		}

	}

	void DrawMenuTelemetryPanel(void* a_context) noexcept
	{
		const auto& panel = *static_cast<const TelemetryPanelDefinition*>(a_context);
		RefreshCache();

		Title(panel.name);
		Muted(panel.description);
		ImGui::Separator();
		if (!s_cache.hasData)
		{
			Muted("Waiting for the first sample."sv);
			DrawPanelFooter();
			return;
		}

		if (panel.panel == TelemetryPanel::kOverview)
		{
			DrawOverviewStatus();
			ImGui::Spacing();
			DrawFrameHistory();
			ImGui::Spacing();
			DrawOverviewMetrics();
			ImGui::Spacing();
			DrawFrameRecords();
		}
		else
		{
			for (const auto& group : kTelemetryMetricGroups)
			{
				if (group.panel != panel.panel)
					continue;
				DrawMetricGroup(group);
				ImGui::Spacing();
			}
			if (panel.panel == TelemetryPanel::kDecompression)
				DrawSeries();
		}
		DrawPanelFooter();
	}
}
