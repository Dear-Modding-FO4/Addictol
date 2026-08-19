#pragma once

#include <AdModule.h>
#include <AdTelemetry.h>

namespace Addictol
{
	struct ZlibInflateOutcome;

	class ModuleLibDeflate :
		public Module,
		public MetricSource,
		public SeriesSource
	{
	public:
		inline static constexpr auto kSizeBucketLabels = [] {
			std::array<std::string_view, kZlibSizeBuckets.size()> labels{};
			for (size_t index = 0; index < labels.size(); ++index)
				labels[index] = kZlibSizeBuckets[index].label;
			return labels;
		}();
		inline static constexpr std::array<std::string_view, 2> kThreadBucketLabels{
			"render",
			"worker"
		};
		inline static constexpr std::array<std::string_view, 6> kFlushBucketLabels{
			"no_flush",
			"sync",
			"finish",
			"block",
			"trees",
			"other"
		};
		inline static constexpr size_t kSeriesCapacity{
			kSizeBucketLabels.size() * 4 +
			kThreadBucketLabels.size() * 2 +
			kFlushBucketLabels.size()
		};

		ModuleLibDeflate();
		virtual ~ModuleLibDeflate() = default;

		[[nodiscard]] virtual const REX::TOML::Bool<>* GetOption() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;
		[[nodiscard]] size_t SeriesCapacity() const noexcept override;
		static void Record(
			const ZlibInflateOutcome& a_outcome,
			bool a_telemetryEnabled,
			int32_t a_flush,
			uint32_t a_currentThreadId,
			uint64_t a_bytesIn,
			uint64_t a_bytesOut) noexcept;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;
		[[nodiscard]] size_t DrainSeries(
			std::span<SeriesSample> a_out) noexcept override;

		inline static ModuleLibDeflate* s_instance{ nullptr };
		ZlibIntervalCounters m_interval{};
		std::atomic<bool> m_active{ false };
	};
}