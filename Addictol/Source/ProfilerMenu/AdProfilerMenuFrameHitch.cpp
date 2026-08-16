#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

#include <array>
#include <cfloat>

namespace Addictol
{
	namespace
	{
		using namespace ProfilerMenuUi;

		void DrawSeries(
			const std::array<float, kFrameHitchProfileEntryCapacity>& a_values,
			size_t a_count,
			std::string_view a_label) noexcept
		{
			if (!a_count)
				return;

			ImGui::PlotLines(
				a_label.data(),
				a_values.data(),
				static_cast<int>(a_count),
				0,
				nullptr,
				FLT_MAX,
				FLT_MAX,
				ImVec2(0.0f, 60.0f));
		}

		void DrawStalls(const FrameHitchProfileEntry& a_interval) noexcept
		{
			Heading("Loading stalls"sv);
			LabeledValue(
				"LoadQueuedPriority"sv,
				FormatMs(a_interval.loadQueuedPriority.totalMs));
			LabeledValue("LoadQueuedPriority calls"sv, FormatCount(a_interval.loadQueuedPriority.calls));
			LabeledValue("ClearLoadingTask"sv, FormatMs(a_interval.clearLoadingTask.totalMs));
			LabeledValue("ClearLoadingTask calls"sv, FormatCount(a_interval.clearLoadingTask.calls));
		}

		void DrawPhases(const FrameHitchProfileEntry& a_interval) noexcept
		{
			Heading("Engine phases"sv);
			if (!ImGui::BeginTable("frame_phases", 3, kTableFlags, ImVec2(0.0f, 220.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (size_t index = 0; index < a_interval.phases.size(); ++index)
			{
				const auto& phase = a_interval.phases[index];
				const auto name = FrameHitchPhaseName(index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableNextColumn();
				MonoCell(FormatMs(phase.totalMs));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(phase.calls));
			}
			ImGui::EndTable();
		}

		void DrawHitchRows(const FrameHitchProfileEntry& a_interval) noexcept
		{
			Heading("Captured hitches"sv);
			if (a_interval.hitches.empty())
			{
				Muted("No hitch crossed the configured threshold in this interval."sv);
				return;
			}

			if (!ImGui::BeginTable("frame_hitches", 5, kTableFlags, ImVec2(0.0f, 220.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Sequence", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Stall", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Stall calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Window frames", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(a_interval.hitches.size()));
			while (clipper.Step())
			{
				for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const auto& window = a_interval.hitches[static_cast<size_t>(row)];
					const auto& hitch = window.hitch;
					const auto stallMs =
						hitch.loadQueuedPriority.totalMs + hitch.clearLoadingTask.totalMs;
					const auto stallCalls =
						hitch.loadQueuedPriority.calls + hitch.clearLoadingTask.calls;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					MonoCell(FormatCount(hitch.sequence));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(hitch.frameMs));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(stallMs));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(stallCalls));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(window.frames.size()));
				}
			}
			ImGui::EndTable();
		}

		void DrawIntervalRows(const ProfilerMenuFrameHitchCache& a_cache) noexcept
		{
			Heading("Retained intervals"sv);
			if (!ImGui::BeginTable("frame_intervals", 7, kTableFlags, ImVec2(0.0f, 200.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Mean", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Hitches", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(a_cache.intervals.size()));
			while (clipper.Step())
			{
				for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const auto& interval = a_cache.intervals[static_cast<size_t>(row)];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					MonoCell(FormatCount(interval.frameCount));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.meanMs));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.p95Ms));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.p99Ms));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.maxMs));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(interval.percentileSamples));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(interval.hitchCount));
				}
			}
			ImGui::EndTable();
		}
	}

	void DrawProfilerMenuFrameHitch(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		const auto& cache = a_model.FrameHitch();
		if (!cache.hasLatest)
		{
			Muted("No frame-hitch interval has been published yet. Requires bFrameHitchProfiler."sv);
			PanelFooter(cache.state, a_context);
			return;
		}

		const auto& latest = cache.latest;

		Heading("Retained interval percentiles"sv);
		DrawSeries(cache.mean, cache.intervalCount, "mean ms"sv);
		DrawSeries(cache.p95, cache.intervalCount, "p95 ms"sv);
		DrawSeries(cache.p99, cache.intervalCount, "p99 ms"sv);
		DrawSeries(cache.max, cache.intervalCount, "max ms"sv);

		ImGui::Separator();
		LabeledValue("Latest frames"sv, FormatCount(latest.frameCount));
		LabeledValue("Latest mean"sv, FormatMs(latest.meanMs));
		LabeledValue("Latest P95"sv, FormatMs(latest.p95Ms));
		LabeledValue("Latest P99"sv, FormatMs(latest.p99Ms));
		LabeledValue("Latest max"sv, FormatMs(latest.maxMs));
		LabeledValue("Percentile samples"sv, FormatCount(latest.percentileSamples));

		if (latest.droppedSamples)
			Warn("Frame samples were dropped; percentiles cover the retained samples only."sv);
		if (latest.droppedHitches)
			Warn("Hitch windows were dropped; the captured list is incomplete."sv);

		ImGui::Spacing();
		DrawStalls(latest);
		ImGui::Spacing();
		DrawPhases(latest);
		ImGui::Spacing();
		DrawHitchRows(latest);
		ImGui::Spacing();
		DrawIntervalRows(cache);

		PanelFooter(cache.state, a_context);
	}
}
