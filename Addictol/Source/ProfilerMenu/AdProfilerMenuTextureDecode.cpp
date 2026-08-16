#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

namespace Addictol
{
	using namespace ProfilerMenuUi;

	void DrawProfilerMenuTextureDecode(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		const auto& cache = a_model.Texture();
		const auto& counters = cache.counters;
		const auto installed = counters.installState == TextureOneShot::InstallState::Installed;

		LabeledState("Seam"sv, installed, TextureOneShot::Describe(counters.installState));
		Muted("Every counter below is cumulative for the process and is never reset while it runs."sv);
		ImGui::Separator();

		Heading("Requests"sv);
		LabeledValue("Requests"sv, FormatCount(counters.requests));
		LabeledValue("Served one-shot"sv, FormatRatio(counters.oneShotRequests, counters.requests));
		LabeledValue("Fell back"sv, FormatRatio(counters.fallbackRequests, counters.requests));
		LabeledValue("Stock replay failures"sv, FormatCount(counters.fallbackFailures));
		LabeledValue("Delegated"sv, FormatCount(counters.delegations));
		LabeledValue("Nested delegations"sv, FormatCount(counters.nestedDelegations));
		LabeledValue("Unknown-caller delegations"sv, FormatCount(counters.unknownCallerDelegations));

		ImGui::Spacing();
		Heading("Chunks"sv);
		LabeledValue("Observed"sv, FormatCount(counters.chunksObserved));
		LabeledValue("Decoded"sv, FormatCount(counters.chunksDecoded));
		LabeledValue("Decoded bytes"sv, FormatBytes(counters.decodedBytes));
		LabeledValue("Size samples"sv, FormatCount(counters.sizeSamples));
		LabeledValue(
			"fullSize mismatches"sv,
			FormatRatio(counters.sizeMismatches, counters.sizeSamples));
		LabeledValue("Delta minimum"sv, FormatSigned(counters.minSizeDelta));
		LabeledValue("Delta maximum"sv, FormatSigned(counters.maxSizeDelta));
		LabeledValue("Nominal-vs-descriptor"sv, FormatCount(counters.nominalDescMismatches));
		LabeledValue("Zero compressed chunks"sv, FormatCount(counters.zeroCompressedChunks));
		LabeledValue("Bad chunk headers"sv, FormatCount(counters.badChunkHeaders));
		LabeledValue("Capacity failures"sv, FormatCount(counters.capacityFailures));
		LabeledValue("Row buffer unavailable"sv, FormatCount(counters.rowBufferUnavailable));
		LabeledValue("Attribution failures"sv, FormatCount(counters.attributionFailures));

		if (counters.firstMismatchPublished)
		{
			ImGui::Spacing();
			Heading("First fullSize mismatch"sv);
			LabeledValue(
				"Chunk"sv,
				FormatRatio(counters.firstMismatchChunk, counters.firstMismatchChunkCount));
			LabeledValue("Nominal"sv, FormatBytes(counters.firstMismatchNominal));
			LabeledValue("Decoded"sv, FormatBytes(counters.firstMismatchActual));
			LabeledValue("Delta"sv, FormatSigned(counters.firstMismatchDelta));
		}

		ImGui::Spacing();
		Heading("Fallback reasons"sv);
		if (ImGui::BeginTable("texture_reasons", 2, ProfilerMenuUi::kTableFlags, ImVec2(0.0f, 220.0f)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Requests", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (size_t reason = 0; reason < counters.fallbackReasons.size(); ++reason)
			{
				const auto name = BA2Profile::ReasonName(static_cast<BA2Profile::FallbackReasonId>(reason));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableNextColumn();
				MonoCell(FormatCount(counters.fallbackReasons[reason]));
			}
			ImGui::EndTable();
		}

		if (!installed)
			Muted("The seam is not installed, so these counters stay at their startup values."sv);
		if (counters.sizeMismatches)
			Warn("Chunks decoded to a size other than the archive fullSize; those requests were handed back."sv);
		if (counters.attributionFailures)
			Error("Stock replays could not be attributed; those requests were withheld from the profiler."sv);

		PanelFooter(cache.state, a_context);
	}
}
