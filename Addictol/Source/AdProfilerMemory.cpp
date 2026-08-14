#include <AdProfilerMemory.h>
#include <AdProfilerCore.h>

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <limits>

#pragma comment(lib, "Psapi.lib")

namespace Addictol
{
	[[nodiscard]] static std::int64_t SignedByteDelta(
		std::size_t a_current,
		std::size_t a_baseline) noexcept
	{
		constexpr auto maxDelta =
			static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
		if (a_current >= a_baseline)
			return static_cast<std::int64_t>(std::min(a_current - a_baseline, maxDelta));
		return -static_cast<std::int64_t>(std::min(a_baseline - a_current, maxDelta));
	}

	void ProfilerMemory::CaptureBaseline() noexcept
	{
		if (!ProfilerCore::GetSingleton()->IsActive())
			return;

		PROCESS_MEMORY_COUNTERS pmc{};
		pmc.cb = sizeof(pmc);

		if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		{
			REX::WARN("[Profiler] Failed to capture memory baseline (error: {})"sv,
				GetLastError());
			return;
		}

		m_baselineWorkingSet = static_cast<std::size_t>(pmc.WorkingSetSize);
		m_baselineCommit = static_cast<std::size_t>(pmc.PagefileUsage);
		m_baselinePeakWorkingSet = static_cast<std::size_t>(pmc.PeakWorkingSetSize);
		m_baselineCaptured = true;

		REX::INFO(
			"[Profiler] Memory baseline captured: working set {} bytes, commit {} bytes, peak working set {} bytes"sv,
			m_baselineWorkingSet,
			m_baselineCommit,
			m_baselinePeakWorkingSet);

		// The baseline is also the zero-delta first snapshot.
		CaptureSnapshot("Baseline"sv);
	}

	void ProfilerMemory::CaptureSnapshot(std::string_view a_phaseName) noexcept
	{
		if (!ProfilerCore::GetSingleton()->IsActive())
			return;

		PROCESS_MEMORY_COUNTERS pmc{};
		pmc.cb = sizeof(pmc);

		if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		{
			REX::WARN("[Profiler] Failed to capture memory snapshot for '{}' (error: {})"sv,
				a_phaseName, GetLastError());
			return;
		}

		const auto workingSetBytes = static_cast<std::size_t>(pmc.WorkingSetSize);
		const auto commitBytes = static_cast<std::size_t>(pmc.PagefileUsage);
		const auto peakWorkingSetBytes = static_cast<std::size_t>(pmc.PeakWorkingSetSize);
		const auto workingSetDeltaBytes = m_baselineCaptured ?
			SignedByteDelta(workingSetBytes, m_baselineWorkingSet) :
			0;

		MemorySnapshot snapshot;
		snapshot.phaseName = std::string(a_phaseName);
		snapshot.workingSetBytes = workingSetBytes;
		snapshot.commitBytes = commitBytes;
		snapshot.peakWorkingSetBytes = peakWorkingSetBytes;
		snapshot.workingSetDeltaBytes = workingSetDeltaBytes;

		ProfilerCore::GetSingleton()->AddMemorySnapshot(std::move(snapshot));
	}
}
