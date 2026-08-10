#pragma once

#include <chrono>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>

#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	// High-resolution scoped timer that records elapsed milliseconds
	class ScopedProfileTimer
	{
		std::chrono::high_resolution_clock::time_point m_start;
		double& m_target;
	public:
		explicit ScopedProfileTimer(double& a_target) noexcept :
			m_start(std::chrono::high_resolution_clock::now()),
			m_target(a_target)
		{}

		~ScopedProfileTimer() noexcept
		{
			auto end = std::chrono::high_resolution_clock::now();
			m_target = std::chrono::duration<double, std::milli>(end - m_start).count();
		}
	};

	// Data structures for profiling results
	struct ESPProfileEntry
	{
		std::string filename;
		std::int32_t loadOrderIndex{ 0 };
		double openMs{ 0.0 };
		double constructMs{ 0.0 };
		double closeMs{ 0.0 };
		double totalMs{ 0.0 };
	};

	struct DLLProfileEntry
	{
		std::string dllName;
		std::string dllPath;
		double queryMs{ 0.0 };
		double loadMs{ 0.0 };
		std::string fileVersion;
	};

	struct ModuleProfileEntry
	{
		std::string moduleName;
		double queryMs{ 0.0 };
		double installMs{ 0.0 };
		bool querySuccess{ false };
		bool installSuccess{ false };
		bool skipped{ false };
	};

	struct StartupPhase
	{
		std::string name;
		std::chrono::high_resolution_clock::time_point timestamp;
		double elapsedFromStartMs{ 0.0 };
	};

	struct MemorySnapshot
	{
		std::string phaseName;
		std::size_t totalAllocated{ 0 };
		std::size_t totalFreed{ 0 };
		std::size_t peakUsage{ 0 };
		std::size_t allocationCount{ 0 };
	};

	struct BA2ProfileEntry
	{
		std::string archiveName;
		double decompressMs{ 0.0 };
		std::size_t compressedSize{ 0 };
		std::size_t uncompressedSize{ 0 };
		double throughputMBps{ 0.0 };
	};

	struct ProfileMetricEntry
	{
		double totalMs{ 0.0 };
		std::uint64_t calls{ 0 };
	};

	struct AnimSubGraphPassProfileEntry
	{
		double totalMs{ 0.0 };
		double maxMs{ 0.0 };
		std::uint64_t calls{ 0 };
		std::uint64_t matchesAdded{ 0 };
	};

	struct AnimSubGraphProfileEntry
	{
		std::uint32_t role{ 0 };
		ProfileMetricEntry request;
		ProfileMetricEntry matched;
		ProfileMetricEntry gather;
		ProfileMetricEntry initialize;
		ProfileMetricEntry load;
		std::uint64_t eligibleCalls{ 0 };
		std::uint64_t projectedHits{ 0 };
		std::uint64_t projectedCalls{ 0 };
		std::uint64_t actualHits{ 0 };
		std::uint64_t actualCalls{ 0 };
		std::uint64_t ineligibleCalls{ 0 };
		std::uint64_t droppedSamples{ 0 };
		AnimSubGraphPassProfileEntry movement;
		AnimSubGraphPassProfileEntry activate1;
		AnimSubGraphPassProfileEntry activate2;
		std::uint64_t rawFilenames{ 0 };
		std::uint64_t uniqueFilenames{ 0 };
		std::uint64_t filenameGathers{ 0 };
	};

	inline constexpr std::size_t kFrameHitchProfilePhaseCount{ 12 };
	inline constexpr std::size_t kAnimSubGraphProfileEntryCapacity{ 256 };
	inline constexpr std::size_t kFrameHitchProfileEntryCapacity{ 32 };

	struct FrameHitchFrameProfileEntry
	{
		std::uint64_t sequence{ 0 };
		double frameMs{ 0.0 };
		ProfileMetricEntry loadQueuedPriority;
		ProfileMetricEntry clearLoadingTask;
		std::array<ProfileMetricEntry, kFrameHitchProfilePhaseCount> phases;
	};

	struct FrameHitchWindowProfileEntry
	{
		FrameHitchFrameProfileEntry hitch;
		std::vector<FrameHitchFrameProfileEntry> frames;
	};

	struct FrameHitchProfileEntry
	{
		std::uint64_t frameCount{ 0 };
		double meanMs{ 0.0 };
		double p95Ms{ 0.0 };
		double p99Ms{ 0.0 };
		double maxMs{ 0.0 };
		std::size_t percentileSamples{ 0 };
		std::uint64_t droppedSamples{ 0 };
		ProfileMetricEntry loadQueuedPriority;
		ProfileMetricEntry clearLoadingTask;
		std::array<ProfileMetricEntry, kFrameHitchProfilePhaseCount> phases;
		std::vector<FrameHitchWindowProfileEntry> hitches;
		std::uint64_t droppedHitches{ 0 };
	};

	// Central profiler data collector
	class ProfilerCore :
		public REX::Singleton<ProfilerCore>
	{
		std::chrono::high_resolution_clock::time_point m_startTime;

		// ESP/ESM data
		std::vector<ESPProfileEntry> m_espEntries;
		double m_totalCompileMs{ 0.0 };
		double m_initAllFormsMs{ 0.0 };
		std::mutex m_espMutex;

		// DLL data
		std::vector<DLLProfileEntry> m_dllEntries;
		std::mutex m_dllMutex;

		// Module timing data
		std::vector<ModuleProfileEntry> m_moduleEntries;
		std::mutex m_moduleMutex;

		// Startup timeline
		std::vector<StartupPhase> m_startupPhases;
		std::mutex m_startupMutex;

		// Memory snapshots
		std::vector<MemorySnapshot> m_memorySnapshots;
		std::mutex m_memoryMutex;

		// BA2 data
		std::vector<BA2ProfileEntry> m_ba2Entries;
		std::mutex m_ba2Mutex;

		ProfilerCore(const ProfilerCore&) = delete;
		ProfilerCore& operator=(const ProfilerCore&) = delete;
	public:
		ProfilerCore() = default;
		virtual ~ProfilerCore() = default;

		void Start() noexcept;
		[[nodiscard]] bool IsActive() const noexcept;
		[[nodiscard]] static bool IsEnabledInConfig() noexcept;
		[[nodiscard]] static bool IsESPEnabled() noexcept;
		[[nodiscard]] static bool IsDLLEnabled() noexcept;
		[[nodiscard]] static bool IsModuleProfilingEnabled() noexcept;
		[[nodiscard]] static bool IsStartupTimelineEnabled() noexcept;
		[[nodiscard]] static bool IsMemoryTrackingEnabled() noexcept;
		[[nodiscard]] static bool IsBA2TimingEnabled() noexcept;
		[[nodiscard]] static bool IsAnimSubGraphEnabled() noexcept;
		[[nodiscard]] static bool IsFrameHitchEnabled() noexcept;
		[[nodiscard]] static bool IsCSVExportEnabled() noexcept;

		// Startup timeline
		void MarkPhase(std::string_view a_name) noexcept;

		// ESP/ESM profiling
		void AddESPEntry(ESPProfileEntry&& a_entry) noexcept;
		void SetTotalCompileTime(double a_ms) noexcept { m_totalCompileMs = a_ms; }
		void SetInitAllFormsTime(double a_ms) noexcept { m_initAllFormsMs = a_ms; }

		// DLL profiling
		void AddDLLEntry(DLLProfileEntry&& a_entry) noexcept;

		// Module profiling
		void AddModuleEntry(ModuleProfileEntry&& a_entry) noexcept;

		// Memory tracking
		void AddMemorySnapshot(MemorySnapshot&& a_snapshot) noexcept;

		// BA2 timing
		void AddBA2Entry(BA2ProfileEntry&& a_entry) noexcept;

		// Startup entries report once at kGameDataReady; runtime intervals stream for the session.
		static void RecordAnimSubGraphRuntimeInterval(AnimSubGraphProfileEntry&& a_entry) noexcept;
		static void RecordFrameHitchRuntimeInterval(FrameHitchProfileEntry&& a_entry) noexcept;
		void AdvanceSaveLoadEpoch() noexcept;

		// Startup report generation
		void GenerateReport() noexcept;
		void ExportCSV() noexcept;

		// Accessors
		[[nodiscard]] const std::vector<ESPProfileEntry>& GetESPEntries() const noexcept { return m_espEntries; }
		[[nodiscard]] const std::vector<DLLProfileEntry>& GetDLLEntries() const noexcept { return m_dllEntries; }
		[[nodiscard]] const std::vector<ModuleProfileEntry>& GetModuleEntries() const noexcept { return m_moduleEntries; }
		[[nodiscard]] const std::vector<StartupPhase>& GetStartupPhases() const noexcept { return m_startupPhases; }
		[[nodiscard]] const std::vector<BA2ProfileEntry>& GetBA2Entries() const noexcept { return m_ba2Entries; }
	private:
		[[nodiscard]] std::string GetOutputDir() const noexcept;
		void LogESPReport() noexcept;
		void LogDLLReport() noexcept;
		void LogModuleReport() noexcept;
		void LogStartupTimeline() noexcept;
		void LogMemoryReport() noexcept;
		void LogBA2Report() noexcept;
	};
}
