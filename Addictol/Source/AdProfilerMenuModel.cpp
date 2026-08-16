#include <AdProfilerMenuModel.h>
#include <AdProfilerFrameHitch.h>
#include <AdProfilerRuntimeChannel.h>

#include <Windows.h>

#include <algorithm>
#include <utility>

#undef ERROR

namespace Addictol
{
	namespace profilerMenuModelDetail
	{
		static void FinishRefresh(ProfilerMenuPanelState& a_state, uint64_t a_startQpc) noexcept
		{
			const auto end = ReadQpc();
			a_state.refreshedAtQpc = end;
			a_state.refreshTicks = end > a_startQpc ? end - a_startQpc : 0;
			a_state.hasData = true;
		}

		[[nodiscard]] static ProfilerMenuStatus ReadStatus() noexcept
		{
			ProfilerMenuStatus status;
			status.sessionID = ProfilerCore::GetSessionID();
			status.saveLoadEpoch = ProfilerCore::GetSaveLoadEpoch();
			status.monotonicUs = ProfilerCore::GetRuntimeSession().Capture().monotonicUs;
			status.profilerActive = ProfilerCore::GetSingleton()->IsActive();
			status.frameHitchEnabled = ProfilerCore::IsFrameHitchEnabled();
			status.frameHitchInstalled = ProfilerFrameHitch::GetSingleton()->IsInstalled();
			status.allocatorEnabled = ProfilerAllocator::IsEnabled();
			status.allocatorInstalled = ProfilerAllocator::GetSingleton()->IsInstalled();
			status.ba2TimingEnabled = ProfilerCore::IsBA2TimingEnabled();
			status.ba2Recording = ProfilerBA2::GetSingleton()->IsRecording();
			status.memoryTrackingEnabled = ProfilerCore::IsMemoryTrackingEnabled();
			status.moduleProfilingEnabled = ProfilerCore::IsModuleProfilingEnabled();
			status.csvExportEnabled = ProfilerCore::IsCSVExportEnabled();
			return status;
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	void ProfilerMenuModel::Reserve() noexcept
	{
		m_frameHitch.intervals.reserve(kFrameHitchProfileEntryCapacity);
		m_allocator.intervals.reserve(kAllocatorProfileEntryCapacity);
		m_memory.snapshots.reserve(32);
		m_modules.entries.reserve(128);
		m_modules.order.reserve(128);
		m_overviewMemoryScratch.reserve(32);
		m_overviewModuleScratch.reserve(128);
	}

	void ProfilerMenuModel::RefreshPanel(
		ProfilerMenuTab a_tab,
		bool a_active,
		uint64_t a_nowQpc,
		uint64_t a_qpcFrequency,
		uint32_t a_refreshMs) noexcept
	{
		const auto refresh = [&](
			ProfilerMenuPanelState& a_state,
			void (ProfilerMenuModel::*a_refresh)() noexcept) noexcept {
			if (!ShouldRefreshPanel(
					true,
					a_active,
					a_state.hasData,
					a_nowQpc,
					a_state.refreshedAtQpc,
					a_qpcFrequency,
					a_refreshMs))
				return;
			(this->*a_refresh)();
		};

		switch (a_tab)
		{
		case ProfilerMenuTab::kFrameHitch:
			refresh(m_frameHitch.state, &ProfilerMenuModel::RefreshFrameHitch);
			break;
		case ProfilerMenuTab::kDecompression:
			refresh(m_decompression.state, &ProfilerMenuModel::RefreshDecompression);
			break;
		case ProfilerMenuTab::kAllocator:
			refresh(m_allocator.state, &ProfilerMenuModel::RefreshAllocator);
			break;
		case ProfilerMenuTab::kMemory:
			refresh(m_memory.state, &ProfilerMenuModel::RefreshMemory);
			break;
		case ProfilerMenuTab::kModules:
			refresh(m_modules.state, &ProfilerMenuModel::RefreshModules);
			break;
		case ProfilerMenuTab::kTextureDecode:
			refresh(m_texture.state, &ProfilerMenuModel::RefreshTexture);
			break;
		default:
			refresh(m_overview.state, &ProfilerMenuModel::RefreshOverview);
			break;
		}
	}

	void ProfilerMenuModel::RefreshOverview() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		m_overview.status = ReadStatus();
		m_overview.hasFrame = ProfilerCore::CopyLatestFrameHitchInterval(m_overview.frame);
		m_overview.hasDecompression =
			ProfilerBA2::GetSingleton()->CopyLatestPublished(m_overview.decompression);
		m_overview.hasAllocator = ProfilerAllocator::CopyLatestInterval(m_overview.allocator);
		m_overview.texture = TextureOneShot::ReadCounters();
		m_overview.logLevel = LogControl::GetLevel();
		m_overview.logFlushLevel = LogControl::GetFlushLevel();
		m_overview.logStats = LogControl::CopyStats();

		ProfilerCore::GetSingleton()->CopyMemorySnapshots(m_overviewMemoryScratch);
		m_overview.hasMemory = !m_overviewMemoryScratch.empty();
		if (m_overview.hasMemory)
			m_overview.memory = m_overviewMemoryScratch.back();

		ProfilerCore::GetSingleton()->CopyModuleEntries(m_overviewModuleScratch);
		m_overview.moduleCount = m_overviewModuleScratch.size();
		m_overview.moduleFailures = 0;
		m_overview.moduleSkips = 0;
		for (const auto& entry : m_overviewModuleScratch)
		{
			if (entry.skipped)
				++m_overview.moduleSkips;
			else if (!entry.querySuccess || !entry.installSuccess)
				++m_overview.moduleFailures;
		}

		FinishRefresh(m_overview.state, start);
	}

	void ProfilerMenuModel::RefreshFrameHitch() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		m_frameHitch.hasLatest =
			ProfilerCore::CopyFrameHitchView(m_frameHitch.latest, m_frameHitch.intervals);

		m_frameHitch.intervalCount =
			std::min(m_frameHitch.intervals.size(), m_frameHitch.mean.size());
		for (size_t index = 0; index < m_frameHitch.intervalCount; ++index)
		{
			const auto& interval = m_frameHitch.intervals[index];
			m_frameHitch.mean[index] = static_cast<float>(interval.meanMs);
			m_frameHitch.p95[index] = static_cast<float>(interval.p95Ms);
			m_frameHitch.p99[index] = static_cast<float>(interval.p99Ms);
			m_frameHitch.max[index] = static_cast<float>(interval.maxMs);
		}

		FinishRefresh(m_frameHitch.state, start);
	}

	void ProfilerMenuModel::RefreshDecompression() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		auto* profiler = ProfilerBA2::GetSingleton();
		m_decompression.recording = profiler->IsRecording();
		if (!profiler->CopyLatestPublished(m_decompression.published))
			m_decompression.published = {};

		FinishRefresh(m_decompression.state, start);
	}

	void ProfilerMenuModel::RefreshAllocator() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		m_allocator.hasLatest = ProfilerAllocator::CopyLatestInterval(m_allocator.latest);
		ProfilerAllocator::CopyIntervals(m_allocator.intervals);

		FinishRefresh(m_allocator.state, start);
	}

	void ProfilerMenuModel::RefreshMemory() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		ProfilerCore::GetSingleton()->CopyMemorySnapshots(m_memory.snapshots);

		FinishRefresh(m_memory.state, start);
	}

	void ProfilerMenuModel::RefreshModules() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		ProfilerCore::GetSingleton()->CopyModuleEntries(m_modules.entries);
		m_modules.order.resize(m_modules.entries.size());
		for (size_t index = 0; index < m_modules.order.size(); ++index)
			m_modules.order[index] = index;

		FinishRefresh(m_modules.state, start);
	}

	void ProfilerMenuModel::RefreshTexture() noexcept
	{
		using namespace profilerMenuModelDetail;

		const auto start = ReadQpc();
		m_texture.counters = TextureOneShot::ReadCounters();

		FinishRefresh(m_texture.state, start);
	}
}
