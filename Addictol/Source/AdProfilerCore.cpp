#include <AdProfilerCore.h>
#include <AdProfilerRuntimeChannel.h>
#include <AdUtils.h>

#include <Windows.h>

#include <fstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace Addictol
{
	static REX::TOML::Bool<> bProfiler{ "Profiler"sv, "bProfiler"sv, false };
	static REX::TOML::Bool<> bESPProfiler{ "Profiler"sv, "bESPProfiler"sv, true };
	static REX::TOML::Bool<> bDLLProfiler{ "Profiler"sv, "bDLLProfiler"sv, true };
	static REX::TOML::Bool<> bModuleProfiler{ "Profiler"sv, "bModuleProfiler"sv, true };
	static REX::TOML::Bool<> bStartupTimeline{ "Profiler"sv, "bStartupTimeline"sv, true };
	static REX::TOML::Bool<> bMemoryTracking{ "Profiler"sv, "bMemoryTracking"sv, true };
	static REX::TOML::Bool<> bBA2Timing{ "Profiler"sv, "bBA2Timing"sv, true };
	static REX::TOML::Bool<> bAnimSubGraphProfiler{ "Profiler"sv, "bAnimSubGraphProfiler"sv, false };
	static REX::TOML::Bool<> bFrameHitchProfiler{ "Profiler"sv, "bFrameHitchProfiler"sv, false };
	static REX::TOML::Bool<> bCSVExport{ "Profiler"sv, "bCSVExport"sv, true };
	static constexpr std::array<const char*, kFrameHitchProfilePhaseCount> s_frameHitchPhaseNames{
		"UpdateIOManager", "GeneralUpdate", "TreeUpdate", "AI", "UpdateMessageBox", "UpdateTimer",
		"PollControls", "UpdateAudio", "UpdateCurrentGridCell", "UpdateSky", "UpdateImageSpace",
		"PostThreadsProcess"
	};

	[[nodiscard]] static std::string MakeProfilerSessionID() noexcept
	{
		const auto wallClock = static_cast<uint64_t>(
			std::chrono::system_clock::now().time_since_epoch().count());
		const auto monotonic = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());
		return std::format(
			"{:016X}-{:08X}-{:016X}"sv, wallClock, GetCurrentProcessId(), monotonic);
	}

	[[nodiscard]] static std::string MakeProfilerOutputDirectory() noexcept
	{
		return std::format("{}Data\\F4SE\\Plugins\\Addictol\\Profiler\\"sv, AdGetRuntimeDirectory());
	}

	static void WriteAnimSubGraphCSVHeader(std::ostream& a_file)
	{
		WriteRuntimeCSVMetadataHeader(a_file);
		a_file << "Role,RequestMs,RequestCalls,MatchedMs,MatchedCalls,GatherMs,GatherCalls,InitializeMs,InitializeCalls,LoadMs,LoadCalls,EligibleCalls,ProjectedHits,ProjectedCalls,ProjectedRate,ActualHits,ActualCalls,ActualRate,IneligibleCalls,DroppedSamples,MovementMs,MovementCalls,MovementMaxMs,MovementMatchesAdded,Activate1Ms,Activate1Calls,Activate1MaxMs,Activate1MatchesAdded,Activate2Ms,Activate2Calls,Activate2MaxMs,Activate2MatchesAdded,RawFilenames,UniqueFilenames,FilenameGathers\n"sv;
	}

	static void WriteAnimSubGraphCSVEntry(
		std::ostream& a_file,
		const AnimSubGraphProfileEntry& a_entry,
		const RuntimeRowMetadata& a_metadata)
	{
		const auto projectedRate = a_entry.projectedCalls ?
			100.0 * static_cast<double>(a_entry.projectedHits) / static_cast<double>(a_entry.projectedCalls) :
			0.0;
		const auto actualRate = a_entry.actualCalls ?
			100.0 * static_cast<double>(a_entry.actualHits) / static_cast<double>(a_entry.actualCalls) :
			0.0;
		WriteRuntimeCSVMetadata(a_file, a_metadata);
		a_file << a_entry.role << ","sv
			<< std::fixed << std::setprecision(3)
			<< a_entry.request.totalMs << ","sv << a_entry.request.calls << ","sv
			<< a_entry.matched.totalMs << ","sv << a_entry.matched.calls << ","sv
			<< a_entry.gather.totalMs << ","sv << a_entry.gather.calls << ","sv
			<< a_entry.initialize.totalMs << ","sv << a_entry.initialize.calls << ","sv
			<< a_entry.load.totalMs << ","sv << a_entry.load.calls << ","sv
			<< a_entry.eligibleCalls << ","sv
			<< a_entry.projectedHits << ","sv << a_entry.projectedCalls << ","sv
			<< std::setprecision(1) << projectedRate << ","sv
			<< a_entry.actualHits << ","sv << a_entry.actualCalls << ","sv << actualRate << ","sv
			<< a_entry.ineligibleCalls << ","sv << a_entry.droppedSamples << ","sv
			<< std::setprecision(3)
			<< a_entry.movement.totalMs << ","sv << a_entry.movement.calls << ","sv
			<< a_entry.movement.maxMs << ","sv << a_entry.movement.matchesAdded << ","sv
			<< a_entry.activate1.totalMs << ","sv << a_entry.activate1.calls << ","sv
			<< a_entry.activate1.maxMs << ","sv << a_entry.activate1.matchesAdded << ","sv
			<< a_entry.activate2.totalMs << ","sv << a_entry.activate2.calls << ","sv
			<< a_entry.activate2.maxMs << ","sv << a_entry.activate2.matchesAdded << ","sv
			<< a_entry.rawFilenames << ","sv << a_entry.uniqueFilenames << ","sv
			<< a_entry.filenameGathers << "\n"sv;
	}

	static void WriteFrameHitchCSVHeader(std::ostream& a_file)
	{
		WriteRuntimeCSVMetadataHeader(a_file);
		a_file << "RecordType,Sequence,ParentHitchSequence,Name,DurationMs,StallMs,Calls,FrameCount,MeanMs,P95Ms,P99Ms,MaxMs,SampleCount,DroppedSamples,DroppedHitches\n"sv;
	}

	static void WriteFrameHitchCSVEntry(
		std::ostream& a_file,
		const FrameHitchProfileEntry& a_entry,
		const RuntimeRowMetadata& a_metadata)
	{
		WriteRuntimeCSVMetadata(a_file, a_metadata);
		a_file << "Summary,,,,,,,"sv
			<< a_entry.frameCount << ","sv
			<< std::fixed << std::setprecision(3)
			<< a_entry.meanMs << ","sv << a_entry.p95Ms << ","sv << a_entry.p99Ms << ","sv
			<< a_entry.maxMs << ","sv << a_entry.percentileSamples << ","sv
			<< a_entry.droppedSamples << ","sv << a_entry.droppedHitches << "\n"sv;
		WriteRuntimeCSVMetadata(a_file, a_metadata);
		a_file << "SummaryStall,,,LoadQueuedPriority,"sv
			<< a_entry.loadQueuedPriority.totalMs << ",,"sv
			<< a_entry.loadQueuedPriority.calls << "\n"sv;
		WriteRuntimeCSVMetadata(a_file, a_metadata);
		a_file << "SummaryStall,,,ClearLoadingTask,"sv
			<< a_entry.clearLoadingTask.totalMs << ",,"sv
			<< a_entry.clearLoadingTask.calls << "\n"sv;
		for (size_t phaseIndex = 0; phaseIndex < a_entry.phases.size(); ++phaseIndex)
		{
			const auto& metric = a_entry.phases[phaseIndex];
			if (metric.calls)
			{
				WriteRuntimeCSVMetadata(a_file, a_metadata);
				a_file << "SummaryPhase,,,"sv << s_frameHitchPhaseNames[phaseIndex] << ","sv
					<< metric.totalMs << ",,"sv << metric.calls << "\n"sv;
			}
		}
		for (const auto& window : a_entry.hitches)
		{
			const auto& hitch = window.hitch;
			WriteRuntimeCSVMetadata(a_file, a_metadata);
			a_file << "Hitch,"sv << hitch.sequence << ",,,"sv
				<< hitch.frameMs << ",,\n"sv;
			WriteRuntimeCSVMetadata(a_file, a_metadata);
			a_file << "HitchStall,"sv << hitch.sequence << ",,LoadQueuedPriority,"sv
				<< hitch.loadQueuedPriority.totalMs << ",,"sv
				<< hitch.loadQueuedPriority.calls << "\n"sv;
			WriteRuntimeCSVMetadata(a_file, a_metadata);
			a_file << "HitchStall,"sv << hitch.sequence << ",,ClearLoadingTask,"sv
				<< hitch.clearLoadingTask.totalMs << ",,"sv
				<< hitch.clearLoadingTask.calls << "\n"sv;
			for (size_t phaseIndex = 0; phaseIndex < hitch.phases.size(); ++phaseIndex)
			{
				const auto& metric = hitch.phases[phaseIndex];
				if (metric.calls)
				{
					WriteRuntimeCSVMetadata(a_file, a_metadata);
					a_file << "HitchPhase,"sv << hitch.sequence << ",,"sv
						<< s_frameHitchPhaseNames[phaseIndex] << ","sv
						<< metric.totalMs << ",,"sv << metric.calls << "\n"sv;
				}
			}
			for (const auto& frame : window.frames)
			{
				WriteRuntimeCSVMetadata(a_file, a_metadata);
				a_file << "WindowFrame,"sv << frame.sequence << ","sv
					<< hitch.sequence << ",,"sv << frame.frameMs << ","sv
					<< frame.loadQueuedPriority.totalMs + frame.clearLoadingTask.totalMs << ","sv
					<< frame.loadQueuedPriority.calls + frame.clearLoadingTask.calls << "\n"sv;
			}
		}
	}

	struct RuntimeCollector
	{
		RuntimeSessionContext session;
		RuntimeChannel<AnimSubGraphProfileEntry> animSubGraph{
			session,
			kAnimSubGraphProfileEntryCapacity,
			"anim_subgraph_runtime"sv,
			WriteAnimSubGraphCSVHeader,
			WriteAnimSubGraphCSVEntry
		};
		RuntimeChannel<FrameHitchProfileEntry> frameHitch{
			session,
			kFrameHitchProfileEntryCapacity,
			"frame_hitch_runtime"sv,
			WriteFrameHitchCSVHeader,
			WriteFrameHitchCSVEntry
		};
		bool active{ false };
		bool csvExport{ false };
	};

	[[nodiscard]] static RuntimeCollector& GetRuntimeCollector() noexcept
	{
		// Permanent hooks can record during shutdown, so runtime state is never destroyed.
		static auto* const collector = new RuntimeCollector;
		return *collector;
	}

	void ProfilerCore::Start() noexcept
	{
		auto& runtime = GetRuntimeCollector();
		if (runtime.active)
			return;

		m_startTime = std::chrono::high_resolution_clock::now();
		runtime.session.Start(MakeProfilerSessionID(), MakeProfilerOutputDirectory());
		runtime.csvExport = bCSVExport.GetValue();
		runtime.active = true;
		MarkPhase("ProfilerStart"sv);
		REX::INFO("[Profiler] Performance profiler started"sv);
	}

	bool ProfilerCore::IsActive() const noexcept
	{
		return GetRuntimeCollector().active;
	}

	bool ProfilerCore::IsEnabledInConfig() noexcept
	{
		return bProfiler.GetValue();
	}

	bool ProfilerCore::IsESPEnabled() noexcept
	{
		return bESPProfiler.GetValue();
	}

	bool ProfilerCore::IsDLLEnabled() noexcept
	{
		return bDLLProfiler.GetValue();
	}

	bool ProfilerCore::IsModuleProfilingEnabled() noexcept
	{
		return bModuleProfiler.GetValue();
	}

	bool ProfilerCore::IsStartupTimelineEnabled() noexcept
	{
		return bStartupTimeline.GetValue();
	}

	bool ProfilerCore::IsMemoryTrackingEnabled() noexcept
	{
		return bMemoryTracking.GetValue();
	}

	bool ProfilerCore::IsBA2TimingEnabled() noexcept
	{
		return bBA2Timing.GetValue();
	}

	bool ProfilerCore::IsAnimSubGraphEnabled() noexcept
	{
		return bAnimSubGraphProfiler.GetValue();
	}

	bool ProfilerCore::IsFrameHitchEnabled() noexcept
	{
		return bFrameHitchProfiler.GetValue();
	}

	bool ProfilerCore::IsCSVExportEnabled() noexcept
	{
		return bCSVExport.GetValue();
	}

	void ProfilerCore::MarkPhase(std::string_view a_name) noexcept
	{
		if (!IsActive() || !bStartupTimeline.GetValue())
			return;

		auto now = std::chrono::high_resolution_clock::now();
		double elapsed = std::chrono::duration<double, std::milli>(now - m_startTime).count();

		std::lock_guard lock(m_startupMutex);
		m_startupPhases.push_back({ std::string(a_name), now, elapsed });
	}

	void ProfilerCore::AddESPEntry(ESPProfileEntry&& a_entry) noexcept
	{
		if (!IsActive())
			return;

		std::lock_guard lock(m_espMutex);
		m_espEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddDLLEntry(DLLProfileEntry&& a_entry) noexcept
	{
		if (!IsActive())
			return;

		std::lock_guard lock(m_dllMutex);
		m_dllEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddModuleEntry(ModuleProfileEntry&& a_entry) noexcept
	{
		if (!IsActive())
			return;

		std::lock_guard lock(m_moduleMutex);
		m_moduleEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddMemorySnapshot(MemorySnapshot&& a_snapshot) noexcept
	{
		if (!IsActive())
			return;

		std::lock_guard lock(m_memoryMutex);
		m_memorySnapshots.push_back(std::move(a_snapshot));
	}

	void ProfilerCore::RecordAnimSubGraphRuntimeInterval(AnimSubGraphProfileEntry&& a_entry) noexcept
	{
		auto& runtime = GetRuntimeCollector();
		if (!runtime.active)
			return;

		runtime.animSubGraph.Record(std::move(a_entry), runtime.csvExport);
	}

	void ProfilerCore::RecordFrameHitchRuntimeInterval(FrameHitchProfileEntry&& a_entry) noexcept
	{
		auto& runtime = GetRuntimeCollector();
		if (!runtime.active)
			return;

		runtime.frameHitch.Record(std::move(a_entry), runtime.csvExport);
	}

	void ProfilerCore::AdvanceSaveLoadEpoch() noexcept
	{
		auto& runtime = GetRuntimeCollector();
		if (runtime.active)
			runtime.session.AdvanceSaveLoadEpoch();
	}

	RuntimeSessionContext& ProfilerCore::GetRuntimeSession() noexcept
	{
		return GetRuntimeCollector().session;
	}

	std::string ProfilerCore::GetOutputDir() const noexcept
	{
		const auto& dir = GetRuntimeCollector().session.GetOutputDirectory();
		if (dir.empty())
			return {};

		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		return dir;
	}

	void ProfilerCore::LogESPReport() noexcept
	{
		if (m_espEntries.empty())
			return;

		REX::INFO("[Profiler] ===== ESP/ESM Load Time Report ====="sv);
		REX::INFO("[Profiler] Total CompileFiles: {:.1f} ms ({:.2f} s)"sv, m_totalCompileMs, m_totalCompileMs / 1000.0);
		REX::INFO("[Profiler] InitAllForms: {:.1f} ms"sv, m_initAllFormsMs);
		REX::INFO("[Profiler] Files loaded: {}"sv, m_espEntries.size());

		auto sorted = m_espEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.constructMs > b.constructMs; });

		REX::INFO("[Profiler] --- Top files by load time ---"sv);
		size_t reportCount = std::min(sorted.size(), static_cast<size_t>(20));
		for (size_t i = 0; i < reportCount; ++i)
		{
			const auto& e = sorted[i];
			REX::INFO("[Profiler] [{:3d}] {:40s} {:8.1f} ms (open: {:.1f}, construct: {:.1f}, close: {:.1f})"sv,
				e.loadOrderIndex, e.filename, e.totalMs, e.openMs, e.constructMs, e.closeMs);
		}
	}

	void ProfilerCore::LogDLLReport() noexcept
	{
		if (m_dllEntries.empty())
			return;

		REX::INFO("[Profiler] ===== F4SE Plugin DLL Load Time Report ====="sv);

		double totalLoad = 0.0, totalQuery = 0.0;
		for (const auto& e : m_dllEntries)
		{
			totalLoad += e.loadMs;
			totalQuery += e.queryMs;
		}

		REX::INFO("[Profiler] Plugins loaded: {}"sv, m_dllEntries.size());
		REX::INFO("[Profiler] Total Query time: {:.1f} ms"sv, totalQuery);
		REX::INFO("[Profiler] Total Load time: {:.1f} ms"sv, totalLoad);

		auto sorted = m_dllEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.loadMs > b.loadMs; });

		for (const auto& e : sorted)
		{
			REX::INFO("[Profiler]   {:40s} Load: {:8.1f} ms  Query: {:6.1f} ms  Ver: {}"sv,
				e.dllName, e.loadMs, e.queryMs,
				e.fileVersion.empty() ? "(none)"sv : std::string_view(e.fileVersion));
		}
	}

	void ProfilerCore::LogModuleReport() noexcept
	{
		if (m_moduleEntries.empty())
			return;

		REX::INFO("[Profiler] ===== Addictol Module Init Time Report ====="sv);

		double totalQuery = 0.0, totalInstall = 0.0;
		for (const auto& e : m_moduleEntries)
		{
			totalQuery += e.queryMs;
			totalInstall += e.installMs;
		}

		REX::INFO("[Profiler] Modules: {}"sv, m_moduleEntries.size());
		REX::INFO("[Profiler] Total Query time: {:.3f} ms"sv, totalQuery);
		REX::INFO("[Profiler] Total Install time: {:.3f} ms"sv, totalInstall);

		auto sorted = m_moduleEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.installMs > b.installMs; });

		for (const auto& e : sorted)
		{
			const auto status = [&e](bool a_ok) { return a_ok ? "ok"sv : (e.skipped ? "skip"sv : "FAIL"sv); };
			REX::INFO("[Profiler] {:30s} Query: {:8.3f} ms ({})  Install: {:8.3f} ms ({})"sv,
				e.moduleName, e.queryMs, status(e.querySuccess),
				e.installMs, status(e.installSuccess));
		}
	}

	void ProfilerCore::LogStartupTimeline() noexcept
	{
		if (m_startupPhases.empty())
			return;

		REX::INFO("[Profiler] ===== Startup Timeline ====="sv);
		for (size_t i = 0; i < m_startupPhases.size(); ++i)
		{
			const auto& phase = m_startupPhases[i];
			double deltaMs = 0.0;
			if (i > 0)
				deltaMs = phase.elapsedFromStartMs - m_startupPhases[i - 1].elapsedFromStartMs;

			REX::INFO("[Profiler] [{:8.1f} ms] {:30s} (delta: {:8.1f} ms)"sv,
				phase.elapsedFromStartMs, phase.name, deltaMs);
		}
	}

	void ProfilerCore::LogMemoryReport() noexcept
	{
		std::vector<MemorySnapshot> snapshots;
		{
			std::lock_guard lock(m_memoryMutex);
			snapshots = m_memorySnapshots;
		}

		if (snapshots.empty())
			return;

		REX::INFO("[Profiler] ===== Memory Usage Report ====="sv);
		for (const auto& snap : snapshots)
		{
			REX::INFO(
				"[Profiler] {:30s} Working Set: {:>10} bytes  Commit: {:>10} bytes  Peak Working Set: {:>10} bytes  Working Set Delta: {:>11} bytes"sv,
				snap.phaseName,
				snap.workingSetBytes,
				snap.commitBytes,
				snap.peakWorkingSetBytes,
				snap.workingSetDeltaBytes);
		}
	}

	void ProfilerCore::GenerateReport() noexcept
	{
		if (!IsActive())
			return;

		MarkPhase("ReportGeneration"sv);

		REX::INFO("[Profiler] ========================================"sv);
		REX::INFO("[Profiler]   ADDICTOL PERFORMANCE PROFILER REPORT"sv);
		REX::INFO("[Profiler] ========================================"sv);
		REX::INFO("[Profiler] Session ID: {}"sv, GetRuntimeCollector().session.GetSessionID());

		if (bStartupTimeline.GetValue())
			LogStartupTimeline();
		if (bDLLProfiler.GetValue())
			LogDLLReport();
		if (bESPProfiler.GetValue())
			LogESPReport();
		if (bModuleProfiler.GetValue())
			LogModuleReport();
		if (bMemoryTracking.GetValue())
			LogMemoryReport();

		if (bCSVExport.GetValue())
			ExportCSV();

		REX::INFO("[Profiler] ========================================"sv);
		REX::INFO("[Profiler]   END OF PROFILER REPORT"sv);
		REX::INFO("[Profiler] ========================================"sv);
	}

	void ProfilerCore::ExportCSV() noexcept
	{
		auto dir = GetOutputDir();
		if (dir.empty())
			return;
		const auto& sessionID = GetRuntimeCollector().session.GetSessionID();

		auto now = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &now);

		char timeBuf[64];
		std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tm);

		if (!m_espEntries.empty())
		{
			std::string path = std::format("{}esp_load_times_{}.csv"sv, dir, timeBuf);
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "SessionId,LoadOrder,Filename,OpenMs,ConstructMs,CloseMs,TotalMs\n"sv;
				for (const auto& e : m_espEntries)
				{
					file << sessionID << ","sv
						<< e.loadOrderIndex << ","sv
						<< "\""sv << e.filename << "\","sv
						<< std::fixed << std::setprecision(1)
						<< e.openMs << ","sv
						<< e.constructMs << ","sv
						<< e.closeMs << ","sv
						<< e.totalMs << "\n"sv;
				}
				REX::INFO("[Profiler] ESP CSV exported: {}"sv, path);
			}
		}

		if (!m_dllEntries.empty())
		{
			std::string path = std::format("{}dll_load_times_{}.csv"sv, dir, timeBuf);
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "SessionId,DLLName,QueryMs,LoadMs,FileVersion,DLLPath\n"sv;
				for (const auto& e : m_dllEntries)
				{
					file << sessionID << ","sv
						<< "\""sv << e.dllName << "\","sv
						<< std::fixed << std::setprecision(1)
						<< e.queryMs << ","sv
						<< e.loadMs << ","sv
						<< "\""sv << e.fileVersion << "\","sv
						<< "\""sv << e.dllPath << "\"\n"sv;
				}
				REX::INFO("[Profiler] DLL CSV exported: {}"sv, path);
			}
		}

		if (!m_moduleEntries.empty())
		{
			std::string path = std::format("{}module_times_{}.csv"sv, dir, timeBuf);
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "SessionId,ModuleName,QueryMs,QuerySuccess,InstallMs,InstallSuccess,Skipped\n"sv;
				for (const auto& e : m_moduleEntries)
				{
					file << sessionID << ","sv
						<< "\""sv << e.moduleName << "\","sv
						<< std::fixed << std::setprecision(3)
						<< e.queryMs << ","sv
						<< (e.querySuccess ? "true"sv : "false"sv) << ","sv
						<< e.installMs << ","sv
						<< (e.installSuccess ? "true"sv : "false"sv) << ","sv
						<< (e.skipped ? "true"sv : "false"sv) << "\n"sv;
				}
				REX::INFO("[Profiler] Module CSV exported: {}"sv, path);
			}
		}

		if (!m_startupPhases.empty())
		{
			std::string path = std::format("{}startup_timeline_{}.csv"sv, dir, timeBuf);
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "SessionId,Phase,ElapsedMs,DeltaMs\n"sv;
				for (size_t i = 0; i < m_startupPhases.size(); ++i)
				{
					const auto& phase = m_startupPhases[i];
					double delta = (i > 0) ? phase.elapsedFromStartMs - m_startupPhases[i - 1].elapsedFromStartMs : 0.0;
					file << sessionID << ","sv
						<< "\""sv << phase.name << "\","sv
						<< std::fixed << std::setprecision(1)
						<< phase.elapsedFromStartMs << ","sv
						<< delta << "\n"sv;
				}
				REX::INFO("[Profiler] Startup timeline CSV exported: {}"sv, path);
			}
		}
	}
}
