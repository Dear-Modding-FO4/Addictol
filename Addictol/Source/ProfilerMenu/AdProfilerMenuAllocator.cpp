#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

namespace Addictol
{
	namespace
	{
		using namespace ProfilerMenuUi;

		void DrawIntervalBlock(const AllocatorProfileEntry& a_interval) noexcept
		{
			Heading("Latest interval"sv);
			LabeledValue("Frame sequence"sv, FormatCount(a_interval.frameSequence));
			LabeledValue("Frame"sv, FormatMs(a_interval.frameMs));
			LabeledValue("Interval"sv, FormatMs(a_interval.intervalSeconds * 1000.0));
			LabeledValue("Max frame"sv, FormatMs(a_interval.maxFrameMs));
			LabeledValue("Leased slots"sv, FormatCount(a_interval.leasedSlots));
			LabeledValue("Overflowed threads"sv, FormatCount(a_interval.overflowedThreads));
			LabeledValue("Dropped samples (session)"sv, FormatCount(a_interval.droppedSamples));
			LabeledValue("Failed allocations"sv, FormatCount(a_interval.intervalFailedAllocations));
			LabeledValue("Zero-size allocations"sv, FormatCount(a_interval.intervalZeroSizeAllocations));
			LabeledValue("Zero-size frees"sv, FormatCount(a_interval.intervalZeroSizeFrees));
			LabeledValue("Oversize allocations"sv, FormatCount(a_interval.intervalOversizeAllocations));
			LabeledValue(
				"Pool 4096 under 2 KiB"sv,
				FormatCount(a_interval.intervalPool4096Le2048Allocations));

			if (a_interval.spansGap)
				Warn("This interval spans a frame or sample gap; its deltas cover a wider window."sv);
			if (a_interval.overflowedThreads)
				Warn("Threads shared the spill slot; per-thread counts under-report distinct threads."sv);
		}

		void DrawBucketTable(const AllocatorProfileEntry& a_interval) noexcept
		{
			Heading("Size classes"sv);
			if (!ImGui::BeginTable("allocator_buckets", 12, kTableFlags, ImVec2(0.0f, 340.0f)))
				return;

			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Allocs", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Frees", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Live blocks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Live requested", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Allocator bytes", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Overhead", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Granularity waste", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("High-water blocks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("High-water bytes", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Spill allocs (session)", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (size_t index = 0; index < a_interval.buckets.size(); ++index)
			{
				const auto& bucket = a_interval.buckets[index];
				const auto derived = AllocatorBucketDerivedBytes(index, bucket);
				const auto name = AllocatorSizeClassName(index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableNextColumn();
				MonoCell(FormatCount(bucket.allocations));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(bucket.frees));
				ImGui::TableNextColumn();
				MonoCell(FormatSigned(bucket.liveBlocks));
				ImGui::TableNextColumn();
				MonoCell(FormatSignedBytes(bucket.liveBytes));
				ImGui::TableNextColumn();
				MonoCell(FormatSignedBytes(derived.allocatorBytes));
				ImGui::TableNextColumn();
				MonoCell(FormatSignedBytes(derived.overheadBytes));
				ImGui::TableNextColumn();
				MonoCell(FormatSignedBytes(derived.granularityWasteBytes));
				ImGui::TableNextColumn();
				MonoCell(FormatSigned(bucket.highWaterLiveBlocks));
				ImGui::TableNextColumn();
				MonoCell(FormatSignedBytes(bucket.highWaterLiveBytes));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(bucket.touchingThreads));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(bucket.cumulativeSpillAllocations));
			}
			ImGui::EndTable();
		}

		void DrawHistoryTable(const ProfilerMenuAllocatorCache& a_cache) noexcept
		{
			Heading("Retained intervals"sv);
			if (!ImGui::BeginTable("allocator_intervals", 6, kTableFlags, ImVec2(0.0f, 200.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Frame ms", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Max frame ms", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Oversize allocs", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Spans gap", ImGuiTableColumnFlags_WidthStretch);
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
					MonoCell(FormatCount(interval.frameSequence));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.intervalSeconds * 1000.0));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.frameMs));
					ImGui::TableNextColumn();
					MonoCell(FormatMs(interval.maxFrameMs));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(interval.intervalOversizeAllocations));
					ImGui::TableNextColumn();
					MonoCell(FormatBool(interval.spansGap));
				}
			}
			ImGui::EndTable();
		}
	}

	void DrawProfilerMenuAllocator(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		const auto& cache = a_model.Allocator();
		if (!cache.hasLatest)
		{
			Muted("No allocator interval has been published yet. Requires bAllocatorProfiler."sv);
			PanelFooter(cache.state, a_context);
			return;
		}

		Muted("Allocation, free, and anomaly counts are per interval unless marked as session totals."sv);
		ImGui::Separator();
		DrawIntervalBlock(cache.latest);
		ImGui::Spacing();
		DrawBucketTable(cache.latest);
		ImGui::Spacing();
		DrawHistoryTable(cache);

		PanelFooter(cache.state, a_context);
	}
}
