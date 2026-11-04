#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdLoadTiming.h>

#include <atomic>
#include <mutex>

namespace Addictol
{
	using SaveLoadTimingSource = BurstSeriesMetricSource<
		LoadTiming::kSaveMetricSchema.size(),
		LoadTiming::kSaveBurstCapacity>;

	class ModuleSaveLoadTiming :
		public Module,
		public SaveLoadTimingSource
	{
	public:
		ModuleSaveLoadTiming();
		virtual ~ModuleSaveLoadTiming() = default;

		[[nodiscard]] virtual bool DoInstall(
			[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener(
			F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;

		void RecordSpan(
			std::string_view a_series,
			std::string_view a_bucket,
			uint64_t a_from,
			uint64_t a_to) noexcept;
		void OnLoadingMenuClosed() noexcept;

	private:
		template <class, size_t>
		friend uint64_t LoadTiming::TimedForward(uint64_t, uint64_t, uint64_t, uint64_t);
		static void BeginTimedCall(size_t, uint64_t) noexcept {}
		static void EndTimedCall(size_t a_index, uint64_t a_start, uint64_t a_end) noexcept;
		inline static std::array<LoadTiming::ForwardedCall, LoadTiming::kSaveStepNames.size()> s_timedOriginals{};
		inline static ModuleSaveLoadTiming* s_instance{ nullptr };

		// The save-load thread and the main thread both record.
		std::mutex m_recordMutex;
		std::atomic<uint64_t> m_preLoadQpc{ 0 };
		bool m_installed{ false };
		bool m_sinkRegistered{ false };
	};
}
