#pragma once

#include <string_view>

#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	class ProfilerMemory :
		public REX::Singleton<ProfilerMemory>
	{
		std::size_t m_baselineWorkingSet{ 0 };
		std::size_t m_baselineCommit{ 0 };
		std::size_t m_baselinePeakWorkingSet{ 0 };
		bool m_baselineCaptured{ false };

		ProfilerMemory(const ProfilerMemory&) = delete;
		ProfilerMemory& operator=(const ProfilerMemory&) = delete;
	public:
		ProfilerMemory() = default;
		virtual ~ProfilerMemory() = default;

		// Also submits the baseline as the first ProfilerCore snapshot.
		void CaptureBaseline() noexcept;

		// Submits the snapshot to ProfilerCore after computing baseline deltas.
		void CaptureSnapshot(std::string_view a_phaseName) noexcept;

		[[nodiscard]] bool HasBaseline() const noexcept { return m_baselineCaptured; }
		[[nodiscard]] std::size_t GetBaselineWorkingSet() const noexcept { return m_baselineWorkingSet; }
	};
}
