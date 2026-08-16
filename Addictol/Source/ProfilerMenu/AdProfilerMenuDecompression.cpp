#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

namespace Addictol
{
	namespace
	{
		using namespace ProfilerMenuUi;
		using namespace BA2Profile;

		void DrawContextBlock(const BA2PublishedSnapshot& a_published) noexcept
		{
			Heading("Published interval"sv);
			LabeledValue("Reason"sv, a_published.Reason());
			LabeledValue("Sequence"sv, FormatCount(a_published.publishSequence));
			LabeledValue("Save/load epoch"sv, FormatCount(a_published.saveLoadEpoch));
			LabeledValue("Interval"sv, FormatMs(static_cast<double>(a_published.IntervalMicroseconds()) / 1000.0));
			LabeledValue("QPC frequency"sv, FormatCount(a_published.qpcFrequency));
			LabeledValue("Calls"sv, FormatCount(a_published.totals.callsSeen));
			LabeledValue("Total ticks"sv, FormatTicks(a_published.totals.totalQpc, a_published.qpcFrequency));
			LabeledValue("Input bytes"sv, FormatBytes(a_published.totals.inputBytesConsumed));
			LabeledValue("Output bytes"sv, FormatBytes(a_published.totals.outputBytesProduced));
		}

		void DrawBackendTable(const BA2PublishedSnapshot& a_published) noexcept
		{
			Heading("Backends"sv);
			if (!ImGui::BeginTable("ba2_backends", 8, kTableFlags, ImVec2(0.0f, 160.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Backend", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Selected", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Primary", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Primary ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Fallback", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Fallback ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Served", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Served ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (const auto& backend : a_published.totals.backends.entries)
			{
				if (backend.id == kBackendNone)
					continue;

				const auto name = BackendName(backend.id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.selectedCalls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.primaryCalls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.primaryQpc));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.fallbackCalls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.fallbackQpc));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.servedCalls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(backend.servedQpc));
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Unserved");
			ImGui::TableNextColumn();
			MonoCell(FormatCount(a_published.totals.unservedCalls));
			for (int column = 0; column < 6; ++column)
			{
				ImGui::TableNextColumn();
				MonoCell("-"sv);
			}
			ImGui::EndTable();
		}

		void DrawReasonTable(const BA2PublishedSnapshot& a_published) noexcept
		{
			Heading("Fallback reasons"sv);
			if (!ImGui::BeginTable("ba2_reasons", 5, kTableFlags, ImVec2(0.0f, 220.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Primary ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Fallback ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Total ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			const auto& totals = a_published.totals;
			for (size_t reason = 0; reason < kKnownReasonCount; ++reason)
			{
				const auto name = ReasonName(static_cast<FallbackReasonId>(reason));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.reasonCounts[reason]));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.reasonPrimaryQpc[reason]));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.reasonFallbackQpc[reason]));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.reasonTotalQpc[reason]));
			}

			if (totals.unknownReasonCalls)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				Error("Unknown"sv);
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.unknownReasonCalls));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.unknownReasonPrimaryQpc));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.unknownReasonFallbackQpc));
				ImGui::TableNextColumn();
				MonoCell(FormatCount(totals.unknownReasonTotalQpc));
			}
			ImGui::EndTable();
		}

		void DrawBucketTable(const BA2PublishedSnapshot& a_published) noexcept
		{
			Heading("Served output-size buckets"sv);
			if (!ImGui::BeginTable("ba2_buckets", 5, kTableFlags, ImVec2(0.0f, 220.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Backend", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (const auto& backend : a_published.totals.backends.entries)
			{
				if (backend.id == kBackendNone)
					continue;

				for (size_t bucket = 0; bucket < kOutputSizeBucketCount; ++bucket)
				{
					if (!backend.servedBucketCalls[bucket])
						continue;

					const auto backendName = BackendName(backend.id);
					const auto bucketName = kOutputSizeBucketNames[bucket];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(backendName.data(), backendName.data() + backendName.size());
					ImGui::TableNextColumn();
					MonoCell(bucketName);
					ImGui::TableNextColumn();
					MonoCell(FormatCount(backend.servedBucketCalls[bucket]));
					ImGui::TableNextColumn();
					MonoCell(FormatCount(backend.servedBucketQpc[bucket]));
					ImGui::TableNextColumn();
					MonoCell(FormatBytes(backend.servedBucketBytes[bucket]));
				}
			}
			ImGui::EndTable();
		}

		void DrawHealthBlock(const BA2PublishedSnapshot& a_published) noexcept
		{
			const auto& totals = a_published.totals;
			const auto& reconciliation = a_published.reconciliation;

			Heading("Rows and health"sv);
			LabeledValue("Rows written"sv, FormatCount(totals.rowsWritten));
			LabeledValue("Rows dropped"sv, FormatCount(totals.rowsDropped));
			LabeledValue("Rows disabled"sv, FormatCount(totals.rowsDisabled));
			LabeledValue("Leased shards"sv, FormatCount(a_published.leasedShards));
			LabeledValue("Overflowed threads"sv, FormatCount(a_published.overflowedThreads));
			LabeledValue("Spill calls"sv, FormatCount(a_published.spillCalls));
			LabeledValue("Oversized batches"sv, FormatCount(totals.oversizedBatches));
			LabeledValue("Malformed observations"sv, FormatCount(totals.malformedObservations));
			LabeledValue("Backend table overflow"sv, FormatCount(totals.backendTableOverflowCalls));
			LabeledValue(
				"Chunk size mismatches"sv,
				FormatRatio(totals.requests.sizeMismatchChunks, totals.requests.sizeDeltaSamples));
			LabeledValue("Nominal-vs-descriptor"sv, FormatCount(totals.requests.nominalDescMismatches));
			LabeledValue("Capacity failures"sv, FormatCount(totals.requests.capacityFailures));

			Heading("Reconciliation"sv);
			LabeledState("Reasons"sv, reconciliation.reasonPartitionOk, FormatBool(reconciliation.reasonPartitionOk));
			LabeledState("Backends"sv, reconciliation.backendPartitionOk, FormatBool(reconciliation.backendPartitionOk));
			LabeledState("Sites"sv, reconciliation.sitePartitionOk, FormatBool(reconciliation.sitePartitionOk));
			LabeledState("Callers"sv, reconciliation.callerPartitionOk, FormatBool(reconciliation.callerPartitionOk));
			LabeledState("Rows"sv, reconciliation.rowPartitionOk, FormatBool(reconciliation.rowPartitionOk));
			LabeledState("Ticks"sv, reconciliation.tickIdentityOk, FormatBool(reconciliation.tickIdentityOk));
			LabeledState("Request evidence"sv, reconciliation.requestEvidenceOk, FormatBool(reconciliation.requestEvidenceOk));
			LabeledState("Row evidence"sv, reconciliation.rowEvidenceOk, FormatBool(reconciliation.rowEvidenceOk));
			LabeledState("Contract"sv, reconciliation.contractOk, FormatBool(reconciliation.contractOk));

			if (reconciliation.rowsTruncated)
				Warn("Rows were truncated; the retained rows are a biased union of per-shard prefixes."sv);
			if (a_published.spillCalls)
				Warn("Calls used the locked spill shard; their timing may include recorder contention."sv);
			if (a_published.admissionClosed)
				Muted("Admission is closed; this is the final published interval."sv);
			if (!a_published.shutdownPublishEnabled)
				Warn("bSafeExit is disabled, so the final interval cannot be published at shutdown."sv);
		}
	}

	void DrawProfilerMenuDecompression(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		const auto& cache = a_model.Decompression();

		LabeledState("Recording"sv, cache.recording, FormatBool(cache.recording));
		if (!cache.published.valid)
		{
			Muted("No BA2 interval has been published yet. Requires bBA2Timing with bProfiler."sv);
			PanelFooter(cache.state, a_context);
			return;
		}

		Muted("Aggregates come from the last publish; per-call rows are never loaded here."sv);
		ImGui::Separator();
		DrawContextBlock(cache.published);
		ImGui::Spacing();
		DrawBackendTable(cache.published);
		ImGui::Spacing();
		DrawReasonTable(cache.published);
		ImGui::Spacing();
		DrawBucketTable(cache.published);
		ImGui::Spacing();
		DrawHealthBlock(cache.published);

		PanelFooter(cache.state, a_context);
	}
}
