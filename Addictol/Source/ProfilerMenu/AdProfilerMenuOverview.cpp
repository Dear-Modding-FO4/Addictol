#include <AdImguiTheme.h>
#include <AdPlatformImgui.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

namespace Addictol
{
	namespace
	{
		using namespace ProfilerMenuUi;

		void DrawSessionBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Session"sv);
			LabeledValue("Session id"sv, a_cache.status.sessionID.empty() ?
				"inactive"sv :
				std::string_view{ a_cache.status.sessionID });
			LabeledValue("Save/load epoch"sv, FormatCount(a_cache.status.saveLoadEpoch));
			LabeledValue("Session clock"sv, FormatCount(a_cache.status.monotonicUs / 1000));
			LabeledState("Profiler"sv, a_cache.status.profilerActive, FormatBool(a_cache.status.profilerActive));
			LabeledState(
				"Platform"sv,
				PlatformImgui::IsReady(),
				ImguiPlatform::Describe(PlatformImgui::GetInstallState()));
		}

		void DrawSwitchBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Recorders"sv);
			if (!ImGui::BeginTable("overview_recorders", 3, kTableFlags, ImVec2(0.0f, 0.0f)))
				return;

			ImGui::TableSetupColumn("Recorder", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Evidence", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			const auto row = [](
				std::string_view a_name,
				std::string_view a_state,
				std::string_view a_evidence) noexcept {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(a_name.data(), a_name.data() + a_name.size());
				ImGui::TableNextColumn();
				MonoCell(a_state);
				ImGui::TableNextColumn();
				MonoCell(a_evidence);
			};

			row(
				"Frame hitch"sv,
				!a_cache.status.frameHitchEnabled ? "disabled"sv :
					a_cache.status.frameHitchInstalled ? "installed"sv : "not installed"sv,
				a_cache.hasFrame ? "interval"sv : "none"sv);
			row(
				"Allocator"sv,
				!a_cache.status.allocatorEnabled ? "disabled"sv :
					a_cache.status.allocatorInstalled ? "installed"sv : "not installed"sv,
				a_cache.hasAllocator ? "interval"sv : "none"sv);
			row(
				"BA2 decompression"sv,
				!a_cache.status.ba2TimingEnabled ? "disabled"sv :
					a_cache.status.ba2Recording ? "recording"sv : "idle"sv,
				a_cache.hasDecompression ? "publish"sv : "none"sv);
			row(
				"Memory snapshots"sv,
				a_cache.status.memoryTrackingEnabled ? "enabled"sv : "disabled"sv,
				a_cache.hasMemory ? "snapshot"sv : "none"sv);
			row(
				"Modules"sv,
				a_cache.status.moduleProfilingEnabled ? "enabled"sv : "disabled"sv,
				a_cache.moduleCount ? FormatCount(a_cache.moduleCount) : "none"sv);
			row(
				"Texture one-shot"sv,
				TextureOneShot::Describe(a_cache.texture.installState),
				a_cache.texture.requests ? FormatCount(a_cache.texture.requests) : "none"sv);
			row(
				"CSV export"sv,
				a_cache.status.csvExportEnabled ? "enabled"sv : "disabled"sv,
				a_cache.status.csvExportEnabled ? "write-through"sv : "none"sv);
			ImGui::EndTable();
		}

		void DrawLogLevelCombo(
			const char* a_label,
			LogControl::Level& a_current,
			void (*a_setter)(LogControl::Level) noexcept) noexcept
		{
			auto preview = "unknown"sv;
			for (const auto level : kProfilerMenuLogLevels)
			{
				if (level == a_current)
				{
					preview = LogControl::LevelName(level);
					break;
				}
			}
			if (!ImGui::BeginCombo(a_label, preview.data()))
				return;

			for (const auto level : kProfilerMenuLogLevels)
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

		void DrawLogControlBlock(ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Log control"sv);
			Muted("Session-only overrides; they reset when the game exits."sv);
			Muted("[Additional] sLogLevel and sLogFlushLevel are the persistent TOML controls."sv);
			Muted("Record level filters lines; flush level forces synchronous disk writes at that level or higher."sv);
			DrawLogLevelCombo("Record level", a_cache.logLevel, &LogControl::SetLevel);
			DrawLogLevelCombo("Flush level", a_cache.logFlushLevel, &LogControl::SetFlushLevel);
			LabeledValue("Recent output"sv, FormatLinesInLastMinute(a_cache.logStats.linesPerMinute));
			LabeledValue("Lines written (session)"sv, FormatCount(a_cache.logStats.written));
			LabeledValue("Flushes (session)"sv, FormatCount(a_cache.logStats.flushed));
		}

		void DrawFrameBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Latest frame interval"sv);
			if (!a_cache.hasFrame)
			{
				Muted("No frame-hitch interval has been published yet."sv);
				return;
			}

			const auto& frame = a_cache.frame;
			LabeledValue("Frames"sv, FormatCount(frame.frameCount));
			LabeledValue("Mean"sv, FormatMs(frame.meanMs));
			LabeledValue("P95"sv, FormatMs(frame.p95Ms));
			LabeledValue("P99"sv, FormatMs(frame.p99Ms));
			LabeledValue("Max"sv, FormatMs(frame.maxMs));
			if (frame.droppedSamples || frame.droppedHitches)
			{
				Warn("Samples were dropped; see the Frame Hitch panel."sv);
			}
		}

		void DrawMemoryBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Memory"sv);
			if (!a_cache.hasMemory)
			{
				Muted("No memory snapshot has been captured yet."sv);
				return;
			}

			LabeledValue("Phase"sv, a_cache.memory.phaseName);
			LabeledValue("Working set"sv, FormatBytes(a_cache.memory.workingSetBytes));
			LabeledValue("Commit"sv, FormatBytes(a_cache.memory.commitBytes));
			LabeledValue("Peak working set"sv, FormatBytes(a_cache.memory.peakWorkingSetBytes));
			LabeledValue("Working-set delta"sv, FormatSignedBytes(a_cache.memory.workingSetDeltaBytes));
		}

		void DrawDecompressionBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Decompression"sv);
			if (!a_cache.hasDecompression)
			{
				Muted("No BA2 interval has been published yet."sv);
				return;
			}

			const auto& published = a_cache.decompression;
			LabeledValue("Publish"sv, published.Reason());
			LabeledValue("Sequence"sv, FormatCount(published.publishSequence));
			LabeledValue("Calls"sv, FormatCount(published.totals.callsSeen));
			LabeledValue("Total ticks"sv, FormatTicks(published.totals.totalQpc, published.qpcFrequency));
			LabeledValue("Unserved calls"sv, FormatCount(published.totals.unservedCalls));
			LabeledState(
				"Reconciliation"sv,
				published.reconciliation.Ok(),
				FormatBool(published.reconciliation.Ok()));
			if (published.reconciliation.rowsTruncated)
				Warn("Rows were truncated; the retained rows are a biased prefix union."sv);
			if (published.admissionClosed)
				Muted("Admission is closed; this is the final interval."sv);
			if (!published.shutdownPublishEnabled)
				Warn("bSafeExit is disabled, so no interval is published at shutdown."sv);
		}

		void DrawHealthBlock(const ProfilerMenuOverviewCache& a_cache) noexcept
		{
			Heading("Health"sv);
			if (a_cache.hasAllocator)
			{
				const auto& allocator = a_cache.allocator;
				LabeledValue("Allocator interval"sv, FormatMs(allocator.intervalSeconds * 1000.0));
				LabeledValue("Allocator max frame"sv, FormatMs(allocator.maxFrameMs));
				LabeledValue("Failed allocations"sv, FormatCount(allocator.intervalFailedAllocations));
				if (allocator.droppedSamples)
					Warn("Allocator samples were dropped; intervals were coalesced."sv);
				if (allocator.overflowedThreads)
					Warn("Allocator threads overflowed into the shared spill slot."sv);
			}
			else
			{
				Muted("No allocator interval has been published yet."sv);
			}

			const auto& texture = a_cache.texture;
			LabeledValue(
				"Texture one-shot"sv,
				FormatRatio(texture.oneShotRequests, texture.requests));
			if (texture.sizeMismatches)
				Warn("Texture chunks decoded to a size other than the archive fullSize."sv);
			if (texture.attributionFailures)
				Error("Texture replays could not be attributed to their chunks."sv);

			LabeledValue("Modules"sv, FormatCount(a_cache.moduleCount));
			if (a_cache.moduleFailures)
				Warn("Some modules failed query or install; see the Modules panel."sv);
			if (a_cache.moduleSkips)
				Muted("Some modules skipped themselves deliberately."sv);
		}
	}

	void DrawProfilerMenuOverview(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		auto& cache = a_model.MutableOverview();

		Title("Addictol profiler"sv);
		Muted("Every value below is a copy taken at the cache age shown at the bottom of the panel."sv);
		ImGui::Separator();

		if (ImGui::BeginTable("overview_columns", 2, ImGuiTableFlags_None))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			DrawSessionBlock(cache);
			ImGui::Spacing();
			DrawFrameBlock(cache);
			ImGui::Spacing();
			DrawMemoryBlock(cache);
			ImGui::TableNextColumn();
			DrawSwitchBlock(cache);
			ImGui::Spacing();
			DrawLogControlBlock(cache);
			ImGui::Spacing();
			DrawDecompressionBlock(cache);
			ImGui::Spacing();
			DrawHealthBlock(cache);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		Heading("Menu cost"sv);
		LabeledValue("Last draw"sv, FormatMs(a_context.lastDrawMs));
		LabeledValue(
			"Last refresh"sv,
			FormatMs(QpcToMilliseconds(cache.state.refreshTicks, a_context.qpcFrequency)));
		LabeledValue("Cache age"sv, FormatCacheAge(cache.state, a_context));
		LabeledValue("Active panel"sv, Describe(a_context.activeTab));
		LabeledValue("Toggle key"sv, ProfilerMenuToggleKeyName(a_context.toggleKey));

		PanelFooter(cache.state, a_context);
	}
}
