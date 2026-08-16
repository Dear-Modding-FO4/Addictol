#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

namespace Addictol
{
	using namespace ProfilerMenuUi;

	void DrawProfilerMenuMemory(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		const auto& cache = a_model.Memory();
		if (cache.snapshots.empty())
		{
			Muted("No memory snapshot has been captured yet. Requires bMemoryTracking."sv);
			PanelFooter(cache.state, a_context);
			return;
		}

		Muted("Snapshots are captured at lifecycle points; the delta is measured against the baseline."sv);
		ImGui::Separator();

		if (ImGui::BeginTable("memory_snapshots", 5, kTableFlags, ImVec2(0.0f, 420.0f)))
		{
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Working set", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Commit", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Peak working set", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Working-set delta", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(cache.snapshots.size()));
			while (clipper.Step())
			{
				for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const auto& snapshot = cache.snapshots[static_cast<size_t>(row)];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(
						snapshot.phaseName.data(),
						snapshot.phaseName.data() + snapshot.phaseName.size());
					ImGui::TableNextColumn();
					MonoCell(FormatBytes(snapshot.workingSetBytes));
					ImGui::TableNextColumn();
					MonoCell(FormatBytes(snapshot.commitBytes));
					ImGui::TableNextColumn();
					MonoCell(FormatBytes(snapshot.peakWorkingSetBytes));
					ImGui::TableNextColumn();
					MonoCell(FormatSignedBytes(snapshot.workingSetDeltaBytes));
				}
			}
			ImGui::EndTable();
		}

		const auto& latest = cache.snapshots.back();
		ImGui::Spacing();
		Heading("Latest snapshot"sv);
		LabeledValue("Phase"sv, latest.phaseName);
		LabeledValue("Working set"sv, FormatBytes(latest.workingSetBytes));
		LabeledValue("Commit"sv, FormatBytes(latest.commitBytes));
		LabeledValue("Peak working set"sv, FormatBytes(latest.peakWorkingSetBytes));
		LabeledValue("Working-set delta"sv, FormatSignedBytes(latest.workingSetDeltaBytes));
		LabeledValue("Snapshots"sv, FormatCount(cache.snapshots.size()));

		PanelFooter(cache.state, a_context);
	}
}
