#pragma once

#include <Telemetry/AdTelemetry.h>

#include <array>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dmui
{
	class Client;
}

namespace Addictol
{
	class ModuleManager;

#ifdef AD_TELEMETRY_TESTS
	namespace TelemetryTest
	{
		struct HubAccess;
	}
#endif

	struct FrameRecord
	{
		uint64_t qpc;
		uint32_t durationUs;
	};

	struct TelemetrySnapshot
	{
		uint64_t sequence{ 0 };
		uint64_t qpc{ 0 };
		double intervalMs{ 0.0 };
		double latenessMs{ 0.0 };
		std::vector<MetricValue> values{};
		std::vector<SeriesSample> series{};
	};

	struct TelemetryStats
	{
		uint64_t overwrittenSamples{ 0 };
		uint64_t skippedSamples{ 0 };
		uint64_t frameRecordOverflows{ 0 };
	};

	enum class TelemetryRegistration : uint8_t
	{
		kAccepted,
		kFrozen,
		kInvalidSource,
		kInvalidKey,
		kDuplicateKey
	};

	class TelemetryHub
	{
	public:
		explicit TelemetryHub(uint64_t a_qpcFrequency = 0) noexcept;
		~TelemetryHub();

		[[nodiscard]] TelemetryRegistration Register(std::shared_ptr<MetricSource> a_source) noexcept;
		[[nodiscard]] bool Freeze(size_t a_ringCapacity = 120) noexcept;
		[[nodiscard]] bool Start(
			uint32_t a_cadenceMs,
			std::filesystem::path a_csvPath = {},
			std::filesystem::path a_seriesCsvPath = {}) noexcept;
		void Stop() noexcept;
		void PushFrameRecord(uint64_t a_qpc, uint32_t a_durationUs) noexcept;

		[[nodiscard]] std::span<const MetricDescriptor> Columns() const noexcept;
		[[nodiscard]] bool CopyLatest(TelemetrySnapshot& a_out) const noexcept;
		[[nodiscard]] size_t CopyMetricHistory(
			size_t a_column,
			std::span<MetricValue> a_out) const noexcept;
		[[nodiscard]] size_t CopyFrameRecords(std::span<FrameRecord> a_out) const noexcept;
		[[nodiscard]] TelemetryStats Stats() const noexcept;

		static bool WriteCsvHeader(std::ostream& a_stream, std::span<const MetricDescriptor> a_columns);
		static bool WriteCsvRow(std::ostream& a_stream, const TelemetrySnapshot& a_snapshot);
		static bool WriteSeriesCsvHeader(std::ostream& a_stream);
		static bool WriteSeriesCsvRows(
			std::ostream& a_stream,
			uint64_t a_qpc,
			std::span<const SeriesSample> a_samples);

	private:
		struct SourceEntry
		{
			std::shared_ptr<MetricSource> source;
			SeriesSource* seriesSource{ nullptr };
			size_t offset{ 0 };
			size_t count{ 0 };
			size_t seriesOffset{ 0 };
			size_t seriesCapacity{ 0 };
		};

		static constexpr size_t kFrameRecordCapacity{ 64 };
		void Worker() noexcept;
		void ClearIntervals() noexcept;
		[[nodiscard]] TelemetrySnapshot& Collect(
			uint64_t a_qpc,
			uint64_t a_intervalEndQpc,
			double a_intervalMs,
			double a_latenessMs) noexcept;
#ifdef AD_TELEMETRY_TESTS
		friend struct TelemetryTest::HubAccess;
#endif

		uint64_t m_qpcFrequency{ 0 };
		std::vector<SourceEntry> m_sources{};
		std::vector<MetricDescriptor> m_columns{};
		std::vector<TelemetrySnapshot> m_ring{};
		TelemetrySnapshot m_collecting{};
		size_t m_ringWrite{ 0 };
		size_t m_ringCount{ 0 };
		uint64_t m_sequence{ 0 };
		std::atomic<uint64_t> m_overwritten{ 0 };
		std::atomic<uint64_t> m_skipped{ 0 };
		std::atomic<uint64_t> m_frameRecordOverflows{ 0 };
		bool m_frozen{ false };

		mutable std::mutex m_publishMutex{};
		TelemetrySnapshot m_published{};

		mutable std::mutex m_frameRecordMutex{};
		std::array<FrameRecord, kFrameRecordCapacity> m_frameRecords{};
		size_t m_frameRecordRead{ 0 };
		size_t m_frameRecordCount{ 0 };

		std::mutex m_workerMutex{};
		std::condition_variable m_workerWake{};
		std::thread m_worker{};
		bool m_stopRequested{ false };
		uint32_t m_cadenceMs{ 0 };
		uint64_t m_workerStartQpc{ 0 };
		std::filesystem::path m_csvPath{};
		std::filesystem::path m_seriesCsvPath{};
	};

	class ProcessMemoryMetricSource final : public MetricSource
	{
	public:
		struct Sample
		{
			uint64_t workingSetBytes;
			uint64_t privateBytes;
			uint64_t peakWorkingSetBytes;
			uint64_t pagefileBytes;
			uint64_t pageFaults;
		};

		using Reader = bool (*)(Sample& a_sample) noexcept;

		explicit ProcessMemoryMetricSource(Reader a_reader = nullptr) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;

		Reader m_reader;
		uint64_t m_previousPageFaults{ 0 };
		bool m_hasPreviousPageFaults{ false };
	};

	class GpuVideoMemoryMetricSource final : public MetricSource
	{
	public:
		using Reader = bool (*)(uint64_t& a_used, uint64_t& a_budget) noexcept;

		explicit GpuVideoMemoryMetricSource(Reader a_reader) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;

		Reader m_reader;
	};

	class SystemMemoryMetricSource final : public MetricSource
	{
	public:
		struct Sample
		{
			uint64_t availablePhysicalBytes;
			uint64_t totalPagefileBytes;
			uint64_t availablePagefileBytes;
		};

		using Reader = bool (*)(Sample& a_sample) noexcept;

		explicit SystemMemoryMetricSource(Reader a_reader = nullptr) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;

		Reader m_reader;
	};

	class FrameMetricSource final : public MetricSource
	{
	public:
		FrameMetricSource(
			TelemetryHub& a_hub,
			uint64_t a_qpcFrequency,
			uint32_t a_frameRecordThresholdMs) noexcept;
		void ObserveAt(uint64_t a_qpc) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;
		void BeginInterval(uint64_t a_qpc) noexcept override;
		void UpdateMaximum(uint32_t a_durationUs, uint32_t a_offsetUs) noexcept;

		TelemetryHub& m_hub;
		uint64_t m_qpcFrequency;
		uint32_t m_frameRecordThresholdUs;
		std::atomic<uint64_t> m_previousQpc{ 0 };
		std::atomic<uint64_t> m_maxAndOffsetUs{ 0 };
		std::atomic<uint32_t> m_minDurationUs{ std::numeric_limits<uint32_t>::max() };
		std::atomic<uint64_t> m_countAndTotalDurationUs{ 0 };
		std::atomic<uint64_t> m_intervalStartQpc{ 0 };
		std::atomic<bool> m_active{ false };
#ifdef AD_TELEMETRY_TESTS
		friend struct TelemetryTest::HubAccess;
#endif
	};

	namespace TelemetryDetail
	{
		using FrameClock = uint64_t (*)() noexcept;
		using ThreadIdReader = uint32_t (*)() noexcept;
		void ObserveFrame(FrameMetricSource* a_source, FrameClock a_clock) noexcept;
		void CaptureRenderThread(ThreadIdReader a_reader) noexcept;
	}

	namespace Telemetry
	{
		[[nodiscard]] TelemetryHub& Hub() noexcept;
		void Initialize(const ModuleManager& a_modules) noexcept;
		[[nodiscard]] bool ConnectDearModdingUI(dmui::Client& a_client) noexcept;
		void ObserveFrame() noexcept;
	}
}
