#include <AdProfilerMemory.h>
#include <AdProfilerCore.h>

#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

namespace Addictol
{
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
		m_baselinePagefile = static_cast<std::size_t>(pmc.PagefileUsage);
		m_baselinePeak = static_cast<std::size_t>(pmc.PeakWorkingSetSize);
		m_baselineCaptured = true;

		REX::INFO("[Profiler] Memory baseline captured: WS={} bytes, PF={} bytes, Peak={} bytes"sv,
			m_baselineWorkingSet, m_baselinePagefile, m_baselinePeak);

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

		auto currentWS = static_cast<std::size_t>(pmc.WorkingSetSize);
		auto currentPF = static_cast<std::size_t>(pmc.PagefileUsage);
		auto currentPeak = static_cast<std::size_t>(pmc.PeakWorkingSetSize);

		// Unsigned deltas clamp working-set shrinkage to zero.
		std::size_t deltaWS = 0;
		if (m_baselineCaptured && currentWS > m_baselineWorkingSet)
			deltaWS = currentWS - m_baselineWorkingSet;

		MemorySnapshot snapshot;
		snapshot.phaseName      = std::string(a_phaseName);
		snapshot.totalAllocated = currentWS;    // WorkingSetSize     (current physical memory)
		snapshot.totalFreed     = currentPF;    // PagefileUsage      (committed virtual memory)
		snapshot.peakUsage      = currentPeak;  // PeakWorkingSetSize (peak physical memory)
		snapshot.allocationCount = deltaWS;     // WS delta from baseline (net growth in bytes)

		ProfilerCore::GetSingleton()->AddMemorySnapshot(std::move(snapshot));
	}
}
