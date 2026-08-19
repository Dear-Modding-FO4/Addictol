#include <AdTelemetryHub.h>

#include <Windows.h>
#include <Psapi.h>
#include <spdlog/spdlog.h>

#include <charconv>
#include <cassert>
#include <cmath>
#include <limits>

namespace Addictol
{
	namespace
	{
		inline constexpr uint32_t kFrameCountBits{ 20 };
		inline constexpr uint64_t kFrameCountMask{ (1ull << kFrameCountBits) - 1 };
		inline constexpr uint64_t kFrameTotalDurationMask{
			(1ull << (64 - kFrameCountBits)) - 1
		};

		std::atomic<bool> s_enabled{ false };
		std::atomic<uint32_t> s_renderThreadId{ 0 };

		[[nodiscard]] bool ValidMetricKey(std::string_view a_key) noexcept
		{
			if (a_key.empty() || a_key.front() == '.' || a_key.back() == '.' ||
				a_key.find('.') == std::string_view::npos)
				return false;
			for (const auto character : a_key)
			{
				if ((character < 'a' || character > 'z') &&
					(character < '0' || character > '9') &&
					character != '_' && character != '.')
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ReadProcessMemory(ProcessMemoryMetricSource::Sample& a_sample) noexcept
		{
			PROCESS_MEMORY_COUNTERS_EX counters{};
			counters.cb = sizeof(counters);
			if (!GetProcessMemoryInfo(
				GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
				sizeof(counters)))
				return false;
			a_sample = {
				counters.WorkingSetSize,
				counters.PrivateUsage,
				counters.PeakWorkingSetSize,
				counters.PagefileUsage,
				counters.PageFaultCount
			};
			return true;
		}

		[[nodiscard]] bool ReadSystemMemory(SystemMemoryMetricSource::Sample& a_sample) noexcept
		{
			MEMORYSTATUSEX status{};
			status.dwLength = sizeof(status);
			if (!GlobalMemoryStatusEx(&status))
				return false;
			a_sample = {
				status.ullAvailPhys,
				status.ullTotalPageFile,
				status.ullAvailPageFile
			};
			return true;
		}

		bool WriteDouble(std::ostream& a_stream, double a_value)
		{
			std::array<char, 64> buffer{};
			const auto result = std::to_chars(
				buffer.data(),
				buffer.data() + buffer.size(),
				a_value,
				std::chars_format::general,
				std::numeric_limits<double>::max_digits10);
			if (result.ec != std::errc{})
				return false;
			a_stream.write(buffer.data(), result.ptr - buffer.data());
			return a_stream.good();
		}

		bool WriteUnsigned(std::ostream& a_stream, uint64_t a_value)
		{
			std::array<char, 32> buffer{};
			const auto result = std::to_chars(
				buffer.data(), buffer.data() + buffer.size(), a_value);
			if (result.ec != std::errc{})
				return false;
			a_stream.write(buffer.data(), result.ptr - buffer.data());
			return a_stream.good();
		}
	}

	uint64_t TelemetryDetail::ReadQpc() noexcept
	{
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return static_cast<uint64_t>(value.QuadPart);
	}

	uint64_t TelemetryDetail::QpcFrequency() noexcept
	{
		static const uint64_t frequency = [] {
			LARGE_INTEGER value{};
			return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ?
				static_cast<uint64_t>(value.QuadPart) : 0;
		}();
		return frequency;
	}

	bool Telemetry::EnabledRelaxed() noexcept
	{
		return s_enabled.load(std::memory_order_relaxed);
	}

	uint32_t Telemetry::RenderThreadIdRelaxed() noexcept
	{
		return s_renderThreadId.load(std::memory_order_relaxed);
	}

	void Telemetry::CaptureRenderThread(uint32_t a_threadId) noexcept
	{
		if (!EnabledRelaxed() || !a_threadId ||
			s_renderThreadId.load(std::memory_order_relaxed))
			return;
		uint32_t expected{ 0 };
		(void)s_renderThreadId.compare_exchange_strong(
			expected, a_threadId, std::memory_order_relaxed);
	}

	void Telemetry::ObserveZlibCall(
		ZlibIntervalCounters& a_counters,
		bool a_enabled,
		ZlibFallbackReason a_fallbackReason,
		bool a_servedByLibDeflate,
		int32_t a_flush,
		uint32_t a_currentThreadId,
		uint64_t a_bytesIn,
		uint64_t a_bytesOut,
		uint64_t a_ticks) noexcept
	{
		if (!a_enabled)
			return;
		a_counters.Observe(a_fallbackReason, a_bytesIn, a_bytesOut);
		a_counters.ObserveSeries(
			a_fallbackReason,
			a_servedByLibDeflate,
			a_flush,
			a_currentThreadId,
			RenderThreadIdRelaxed(),
			a_bytesIn,
			a_bytesOut,
			a_ticks);
	}

	TelemetryHub::TelemetryHub(uint64_t a_qpcFrequency) noexcept :
		m_qpcFrequency(a_qpcFrequency ? a_qpcFrequency : TelemetryDetail::QpcFrequency())
	{}

	TelemetryHub::~TelemetryHub()
	{
		Stop();
	}

	TelemetryRegistration TelemetryHub::Register(std::shared_ptr<MetricSource> a_source) noexcept
	{
		if (m_frozen)
		{
			spdlog::error("Telemetry: source registration rejected after freeze");
			return TelemetryRegistration::kFrozen;
		}
		if (!a_source || a_source->Schema().empty())
		{
			spdlog::error("Telemetry: invalid source registration rejected");
			return TelemetryRegistration::kInvalidSource;
		}
		for (const auto& entry : m_sources)
		{
			if (entry.source.get() == a_source.get())
				return TelemetryRegistration::kAccepted;
		}
		for (const auto& descriptor : a_source->Schema())
		{
			if (!ValidMetricKey(descriptor.key))
			{
				spdlog::error("Telemetry: invalid metric key \"{}\"", descriptor.key);
				return TelemetryRegistration::kInvalidKey;
			}
			for (const auto& entry : m_sources)
			{
				for (const auto& existing : entry.source->Schema())
				{
					if (existing.key == descriptor.key)
					{
						spdlog::error("Telemetry: duplicate metric key \"{}\"", descriptor.key);
						return TelemetryRegistration::kDuplicateKey;
					}
				}
			}
		}

		SourceEntry entry{};
		entry.source = std::move(a_source);
		if (const auto series = dynamic_cast<SeriesSource*>(entry.source.get()))
		{
			entry.seriesSource = series;
			entry.seriesCapacity = series->SeriesCapacity();
		}
		m_sources.push_back(std::move(entry));
		return TelemetryRegistration::kAccepted;
	}

	bool TelemetryHub::Freeze(size_t a_ringCapacity) noexcept
	{
		if (m_frozen)
			return true;
		if (!a_ringCapacity)
			return false;

		try
		{
			m_columns.clear();
			size_t columnCount = 0;
			size_t seriesCapacity = 0;
			for (auto& entry : m_sources)
			{
				entry.offset = columnCount;
				entry.count = entry.source->Schema().size();
				columnCount += entry.count;
				m_columns.insert(
					m_columns.end(),
					entry.source->Schema().begin(),
					entry.source->Schema().end());
				entry.seriesOffset = seriesCapacity;
				if (entry.seriesCapacity >
					std::numeric_limits<size_t>::max() - seriesCapacity)
				{
					m_columns.clear();
					return false;
				}
				seriesCapacity += entry.seriesCapacity;
			}
			m_ring.resize(a_ringCapacity);
			for (auto& snapshot : m_ring)
			{
				snapshot.values.resize(columnCount);
				snapshot.series.resize(seriesCapacity);
			}
			m_collecting.values.resize(columnCount);
			m_collecting.series.resize(seriesCapacity);
			m_published.values.resize(columnCount);
			m_published.series.resize(seriesCapacity);
			m_frozen = true;
			return true;
		}
		catch (...)
		{
			spdlog::error("Telemetry: freeze allocation failed");
			m_columns.clear();
			m_ring.clear();
			m_collecting = {};
			return false;
		}
	}

	bool TelemetryHub::Start(
		uint32_t a_cadenceMs,
		std::filesystem::path a_csvPath,
		std::filesystem::path a_seriesCsvPath) noexcept
	{
		if (!m_frozen || m_worker.joinable())
			return false;
		m_cadenceMs = std::max(a_cadenceMs, 1u);
		try
		{
			m_csvPath = std::move(a_csvPath);
			m_seriesCsvPath = std::move(a_seriesCsvPath);
			ClearIntervals();
			m_stopRequested = false;
			m_workerStartQpc = TelemetryDetail::ReadQpc();
			for (const auto& entry : m_sources)
				entry.source->BeginInterval(m_workerStartQpc);
			s_enabled.store(true, std::memory_order_release);
			m_worker = std::thread(&TelemetryHub::Worker, this);
			return true;
		}
		catch (...)
		{
			s_enabled.store(false, std::memory_order_release);
			spdlog::error("Telemetry: worker could not start");
			return false;
		}
	}

	void TelemetryHub::Stop() noexcept
	{
		s_enabled.store(false, std::memory_order_relaxed);
		{
			const std::lock_guard lock{ m_workerMutex };
			m_stopRequested = true;
		}
		m_workerWake.notify_all();
		if (m_worker.joinable())
			m_worker.join();
		if (m_frozen)
			ClearIntervals();
	}

	void TelemetryHub::ClearIntervals() noexcept
	{
		const std::lock_guard lock{ m_publishMutex };
		for (const auto& entry : m_sources)
		{
			auto out = std::span{ m_published.values }.subspan(entry.offset, entry.count);
			entry.source->Drain(out);
			std::fill(out.begin(), out.end(), MetricValue{});
			if (entry.seriesSource)
			{
				auto seriesOut = std::span{ m_published.series }.subspan(
					entry.seriesOffset, entry.seriesCapacity);
				(void)entry.seriesSource->DrainSeries(seriesOut);
				std::fill(seriesOut.begin(), seriesOut.end(), SeriesSample{});
			}
		}
	}

	TelemetrySnapshot& TelemetryHub::Collect(
		uint64_t a_qpc,
		uint64_t a_intervalEndQpc,
		double a_intervalMs,
		double a_latenessMs) noexcept
	{
		m_collecting.sequence = ++m_sequence;
		m_collecting.qpc = a_qpc;
		m_collecting.intervalMs = a_intervalMs;
		m_collecting.latenessMs = a_latenessMs;
		for (const auto& entry : m_sources)
		{
			entry.source->Drain(
				std::span{ m_collecting.values }.subspan(entry.offset, entry.count));
			if (entry.seriesSource)
			{
				auto sourceOut = std::span{ m_collecting.series }.subspan(
					entry.seriesOffset, entry.seriesCapacity);
				const auto reported = entry.seriesSource->DrainSeries(sourceOut);
				assert(reported <= sourceOut.size());
				const auto count = std::min(reported, sourceOut.size());
				std::fill(sourceOut.begin() + count, sourceOut.end(), SeriesSample{});
			}
			entry.source->BeginInterval(a_intervalEndQpc);
		}

		TelemetrySnapshot* collected{ nullptr };
		{
			const std::lock_guard lock{ m_publishMutex };
			auto& snapshot = m_ring[m_ringWrite];
			collected = &snapshot;
			snapshot.sequence = m_collecting.sequence;
			snapshot.qpc = m_collecting.qpc;
			snapshot.intervalMs = m_collecting.intervalMs;
			snapshot.latenessMs = m_collecting.latenessMs;
			std::swap(snapshot.values, m_collecting.values);
			std::swap(snapshot.series, m_collecting.series);
			m_ringWrite = (m_ringWrite + 1) % m_ring.size();
			if (m_ringCount == m_ring.size())
				m_overwritten.fetch_add(1, std::memory_order_relaxed);
			else
				++m_ringCount;
			m_published.sequence = snapshot.sequence;
			m_published.qpc = snapshot.qpc;
			m_published.intervalMs = snapshot.intervalMs;
			m_published.latenessMs = snapshot.latenessMs;
			std::copy(snapshot.values.begin(), snapshot.values.end(), m_published.values.begin());
			std::copy(snapshot.series.begin(), snapshot.series.end(), m_published.series.begin());
		}
		return *collected;
	}

	void TelemetryHub::PushFrameRecord(uint64_t a_qpc, uint32_t a_durationUs) noexcept
	{
		const std::lock_guard lock{ m_frameRecordMutex };
		if (m_frameRecordCount == m_frameRecords.size())
		{
			m_frameRecordOverflows.fetch_add(1, std::memory_order_relaxed);
			m_frameRecords[m_frameRecordRead] = { a_qpc, a_durationUs };
			m_frameRecordRead = (m_frameRecordRead + 1) % m_frameRecords.size();
			return;
		}
		const auto write =
			(m_frameRecordRead + m_frameRecordCount) % m_frameRecords.size();
		m_frameRecords[write] = { a_qpc, a_durationUs };
		++m_frameRecordCount;
	}

	std::span<const MetricDescriptor> TelemetryHub::Columns() const noexcept
	{
		return m_columns;
	}

	bool TelemetryHub::CopyLatest(TelemetrySnapshot& a_out) const noexcept
	{
		const std::lock_guard lock{ m_publishMutex };
		if (!m_published.sequence)
			return false;
		try
		{
			a_out = m_published;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	size_t TelemetryHub::CopyMetricHistory(
		size_t a_column,
		std::span<MetricValue> a_out) const noexcept
	{
		const std::lock_guard lock{ m_publishMutex };
		if (a_column >= m_columns.size() || m_ring.empty())
			return 0;

		const auto count = std::min(a_out.size(), m_ringCount);
		const auto first = m_ringCount - count;
		const auto oldest =
			(m_ringWrite + m_ring.size() - m_ringCount) % m_ring.size();
		for (size_t index = 0; index < count; ++index)
		{
			const auto source = (oldest + first + index) % m_ring.size();
			a_out[index] = m_ring[source].values[a_column];
		}
		return count;
	}

	size_t TelemetryHub::CopyFrameRecords(std::span<FrameRecord> a_out) const noexcept
	{
		const std::lock_guard lock{ m_frameRecordMutex };
		const auto count = std::min(a_out.size(), m_frameRecordCount);
		const auto first = m_frameRecordCount - count;
		for (size_t index = 0; index < count; ++index)
		{
			const auto source =
				(m_frameRecordRead + first + index) % m_frameRecords.size();
			a_out[index] = m_frameRecords[source];
		}
		return count;
	}

	TelemetryStats TelemetryHub::Stats() const noexcept
	{
		return {
			m_overwritten.load(std::memory_order_relaxed),
			m_skipped.load(std::memory_order_relaxed),
			m_frameRecordOverflows.load(std::memory_order_relaxed)
		};
	}

	bool TelemetryHub::WriteCsvHeader(
		std::ostream& a_stream,
		std::span<const MetricDescriptor> a_columns)
	{
		a_stream << "sequence,qpc,interval_ms,lateness_ms";
		for (const auto& descriptor : a_columns)
			a_stream << ',' << descriptor.key;
		a_stream << '\n';
		return a_stream.good();
	}

	bool TelemetryHub::WriteCsvRow(
		std::ostream& a_stream,
		const TelemetrySnapshot& a_snapshot)
	{
		a_stream << a_snapshot.sequence << ',' << a_snapshot.qpc << ',';
		if (!WriteDouble(a_stream, a_snapshot.intervalMs))
			return false;
		a_stream << ',';
		if (!WriteDouble(a_stream, a_snapshot.latenessMs))
			return false;
		for (const auto& value : a_snapshot.values)
		{
			a_stream << ',';
			if (value.valid && !WriteDouble(a_stream, value.value))
				return false;
		}
		a_stream << '\n';
		return a_stream.good();
	}

	bool TelemetryHub::WriteSeriesCsvHeader(std::ostream& a_stream)
	{
		a_stream << "qpc,series,bucket,calls,ticks,bytes\n";
		return a_stream.good();
	}

	bool TelemetryHub::WriteSeriesCsvRows(
		std::ostream& a_stream,
		uint64_t a_qpc,
		std::span<const SeriesSample> a_samples)
	{
		for (const auto& sample : a_samples)
		{
			if (!sample.calls)
				continue;
			if (!WriteUnsigned(a_stream, a_qpc))
				return false;
			a_stream << ',' << sample.series << ',' << sample.bucket << ',';
			if (!WriteUnsigned(a_stream, sample.calls))
				return false;
			a_stream << ',';
			if (!WriteUnsigned(a_stream, sample.ticks))
				return false;
			a_stream << ',';
			if (!WriteUnsigned(a_stream, sample.bytes))
				return false;
			a_stream << '\n' << std::flush;
			if (!a_stream.good())
				return false;
		}
		return true;
	}

	void TelemetryHub::Worker() noexcept
	{
		std::ofstream csv;
		if (!m_csvPath.empty())
		{
			csv.open(m_csvPath, std::ios::trunc);
			csv.imbue(std::locale::classic());
			if (!csv || !WriteCsvHeader(csv, m_columns) || !(csv << std::flush))
			{
				spdlog::error("Telemetry: CSV export disabled after open or header failure");
				csv.close();
			}
		}
		std::ofstream seriesCsv;
		if (!m_seriesCsvPath.empty())
		{
			seriesCsv.open(m_seriesCsvPath, std::ios::trunc);
			seriesCsv.imbue(std::locale::classic());
			if (!seriesCsv || !WriteSeriesCsvHeader(seriesCsv) ||
				!(seriesCsv << std::flush))
			{
				spdlog::error(
					"Telemetry: series CSV export disabled after open or header failure");
				seriesCsv.close();
			}
		}
		const auto cadence = std::chrono::milliseconds{ m_cadenceMs };
		auto deadline = std::chrono::steady_clock::now() + cadence;
		auto previousQpc = m_workerStartQpc;

		for (;;)
		{
			{
				std::unique_lock lock{ m_workerMutex };
				if (m_workerWake.wait_until(lock, deadline, [this] { return m_stopRequested; }))
					break;
			}

			const auto nowSteady = std::chrono::steady_clock::now();
			const auto nowQpc = TelemetryDetail::ReadQpc();
			const auto intervalMs = m_qpcFrequency ?
				static_cast<double>(nowQpc - previousQpc) * 1000.0 /
					static_cast<double>(m_qpcFrequency) : 0.0;
			const auto late = nowSteady > deadline ? nowSteady - deadline :
				std::chrono::steady_clock::duration::zero();
			const auto latenessMs =
				std::chrono::duration<double, std::milli>{ late }.count();
			const auto& snapshot = Collect(previousQpc, nowQpc, intervalMs, latenessMs);
			previousQpc = nowQpc;

			if (csv.is_open() &&
				(!WriteCsvRow(csv, snapshot) || !(csv << std::flush)))
			{
				spdlog::error("Telemetry: CSV export disabled after write or flush failure");
				csv.close();
			}
			if (seriesCsv.is_open() &&
				!WriteSeriesCsvRows(
					seriesCsv,
					snapshot.qpc,
					snapshot.series))
			{
				spdlog::error(
					"Telemetry: series CSV export disabled after write or flush failure");
				seriesCsv.close();
			}
			deadline += cadence;
			if (nowSteady >= deadline)
			{
				const auto skipped =
					std::chrono::duration_cast<std::chrono::milliseconds>(
						nowSteady - deadline).count() / m_cadenceMs + 1;
				m_skipped.fetch_add(static_cast<uint64_t>(skipped), std::memory_order_relaxed);
				deadline += cadence * skipped;
			}
		}
	}

	ProcessMemoryMetricSource::ProcessMemoryMetricSource(Reader a_reader) noexcept :
		m_reader(a_reader ? a_reader : &ReadProcessMemory)
	{}

	std::span<const MetricDescriptor> ProcessMemoryMetricSource::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "process.working_set_bytes", Unit::kBytes },
			MetricDescriptor{ "process.private_bytes", Unit::kBytes },
			MetricDescriptor{ "process.peak_working_set_bytes", Unit::kBytes },
			MetricDescriptor{ "process.pagefile_bytes", Unit::kBytes },
			MetricDescriptor{ "process.page_faults", Unit::kCount }
		};
		return schema;
	}

	void ProcessMemoryMetricSource::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size())
			return;
		Sample sample{};
		if (!m_reader(sample))
		{
			std::fill(a_out.begin(), a_out.end(), MetricValue{});
			return;
		}
		a_out[0] = { static_cast<double>(sample.workingSetBytes), true };
		a_out[1] = { static_cast<double>(sample.privateBytes), true };
		a_out[2] = { static_cast<double>(sample.peakWorkingSetBytes), true };
		a_out[3] = { static_cast<double>(sample.pagefileBytes), true };
		if (!m_hasPreviousPageFaults)
			a_out[4] = {};
		else
		{
			const auto pageFaults = sample.pageFaults >= m_previousPageFaults ?
				sample.pageFaults - m_previousPageFaults : 0;
			a_out[4] = { static_cast<double>(pageFaults), true };
		}
		m_previousPageFaults = sample.pageFaults;
		m_hasPreviousPageFaults = true;
	}

	GpuVideoMemoryMetricSource::GpuVideoMemoryMetricSource(Reader a_reader) noexcept :
		m_reader(a_reader)
	{}

	std::span<const MetricDescriptor> GpuVideoMemoryMetricSource::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "gpu.vram_used_bytes", Unit::kBytes },
			MetricDescriptor{ "gpu.vram_budget_bytes", Unit::kBytes }
		};
		return schema;
	}

	void GpuVideoMemoryMetricSource::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size())
			return;
		uint64_t used{ 0 };
		uint64_t budget{ 0 };
		const auto valid = m_reader && m_reader(used, budget);
		a_out[0] = { valid ? static_cast<double>(used) : 0.0, valid };
		a_out[1] = { valid ? static_cast<double>(budget) : 0.0, valid };
	}

	SystemMemoryMetricSource::SystemMemoryMetricSource(Reader a_reader) noexcept :
		m_reader(a_reader ? a_reader : &ReadSystemMemory)
	{}

	std::span<const MetricDescriptor> SystemMemoryMetricSource::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "system.available_physical_bytes", Unit::kBytes },
			MetricDescriptor{ "system.commit_used_bytes", Unit::kBytes }
		};
		return schema;
	}

	void SystemMemoryMetricSource::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size())
			return;
		Sample sample{};
		if (!m_reader(sample))
		{
			std::fill(a_out.begin(), a_out.end(), MetricValue{});
			return;
		}
		const auto commitUsed = sample.totalPagefileBytes >= sample.availablePagefileBytes ?
			sample.totalPagefileBytes - sample.availablePagefileBytes : 0;
		a_out[0] = { static_cast<double>(sample.availablePhysicalBytes), true };
		a_out[1] = { static_cast<double>(commitUsed), true };
	}

	FrameMetricSource::FrameMetricSource(
		TelemetryHub& a_hub,
		uint64_t a_qpcFrequency,
		uint32_t a_frameRecordThresholdMs) noexcept :
		m_hub(a_hub),
		m_qpcFrequency(a_qpcFrequency),
		m_frameRecordThresholdUs(a_frameRecordThresholdMs * 1000u)
	{}

	void FrameMetricSource::ObserveAt(uint64_t a_qpc) noexcept
	{
		const auto telemetryEnabled = Telemetry::EnabledRelaxed();
		if (!telemetryEnabled)
			return;
		const auto previous = m_previousQpc.exchange(a_qpc, std::memory_order_relaxed);
		m_active.store(true, std::memory_order_release);
		if (!previous || a_qpc <= previous || !m_qpcFrequency)
			return;

		const auto elapsed = a_qpc - previous;
		const auto durationUs = static_cast<uint32_t>(std::min<uint64_t>(
			elapsed * 1000000ull / m_qpcFrequency,
			std::numeric_limits<uint32_t>::max()));
		const auto intervalStartQpc = m_intervalStartQpc.load(std::memory_order_acquire);
		const auto offsetUs = static_cast<uint32_t>(std::min<uint64_t>(
			intervalStartQpc && a_qpc > intervalStartQpc ?
				(a_qpc - intervalStartQpc) * 1000000ull / m_qpcFrequency : 0,
			std::numeric_limits<uint32_t>::max()));
		UpdateMaximum(durationUs, offsetUs);
		auto minimum = m_minDurationUs.load(std::memory_order_relaxed);
		while (durationUs < minimum &&
			!m_minDurationUs.compare_exchange_weak(
				minimum, durationUs, std::memory_order_relaxed))
		{}

		auto packed = m_countAndTotalDurationUs.load(std::memory_order_relaxed);
		for (;;)
		{
			const auto count = packed & kFrameCountMask;
			const auto totalDurationUs = packed >> kFrameCountBits;
			if (count == kFrameCountMask ||
				durationUs > kFrameTotalDurationMask - totalDurationUs)
				break;
			const auto next =
				((totalDurationUs + durationUs) << kFrameCountBits) | (count + 1);
			if (m_countAndTotalDurationUs.compare_exchange_weak(
				packed, next, std::memory_order_relaxed))
				break;
		}
		if (durationUs > m_frameRecordThresholdUs)
			m_hub.PushFrameRecord(a_qpc, durationUs);
	}

	std::span<const MetricDescriptor> FrameMetricSource::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "frame.max_ms", Unit::kMilliseconds },
			MetricDescriptor{ "frame.count", Unit::kCount },
			MetricDescriptor{ "frame.mean_ms", Unit::kMilliseconds },
			MetricDescriptor{ "frame.min_ms", Unit::kMilliseconds },
			MetricDescriptor{ "frame.max_offset_ms", Unit::kMilliseconds }
		};
		return schema;
	}

	void FrameMetricSource::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size())
			return;
		const auto packed =
			m_countAndTotalDurationUs.exchange(0, std::memory_order_acquire);
		const auto maxAndOffset =
			m_maxAndOffsetUs.exchange(0, std::memory_order_acquire);
		const auto maximum = static_cast<uint32_t>(maxAndOffset >> 32);
		const auto maximumOffset = static_cast<uint32_t>(maxAndOffset);
		const auto minimum = m_minDurationUs.exchange(
			std::numeric_limits<uint32_t>::max(),
			std::memory_order_acquire);
		const auto count = packed & kFrameCountMask;
		const auto totalDurationUs = packed >> kFrameCountBits;
		// m_active lags the first observation
		const auto countValid = count != 0 || m_active.load(std::memory_order_acquire);
		const auto maximumValid = maxAndOffset != 0;
		const auto minimumValid =
			minimum != std::numeric_limits<uint32_t>::max();
		a_out[0] = { static_cast<double>(maximum) / 1000.0, maximumValid };
		a_out[1] = { static_cast<double>(count), countValid };
		a_out[2] = {
			count ? static_cast<double>(totalDurationUs) / static_cast<double>(count) / 1000.0 : 0.0,
			count != 0
		};
		a_out[3] = {
			minimumValid ? static_cast<double>(minimum) / 1000.0 : 0.0,
			minimumValid
		};
		a_out[4] = {
			static_cast<double>(maximumOffset) / 1000.0,
			maximumValid
		};
		if (!Telemetry::EnabledRelaxed())
		{
			m_previousQpc.store(0, std::memory_order_relaxed);
			m_intervalStartQpc.store(0, std::memory_order_relaxed);
			m_active.store(false, std::memory_order_relaxed);
		}
	}

	void FrameMetricSource::BeginInterval(uint64_t a_qpc) noexcept
	{
		m_intervalStartQpc.store(a_qpc, std::memory_order_release);
	}

	void FrameMetricSource::UpdateMaximum(
		uint32_t a_durationUs,
		uint32_t a_offsetUs) noexcept
	{
		auto current = m_maxAndOffsetUs.load(std::memory_order_relaxed);
		while (a_durationUs > static_cast<uint32_t>(current >> 32))
		{
			const auto candidate =
				(static_cast<uint64_t>(a_durationUs) << 32) | a_offsetUs;
			if (m_maxAndOffsetUs.compare_exchange_weak(
				current, candidate, std::memory_order_relaxed))
				break;
		}
	}

	void TelemetryDetail::ObserveFrame(
		FrameMetricSource* a_source,
		FrameClock a_clock) noexcept
	{
		if (!Telemetry::EnabledRelaxed())
			return;
		if (a_source && a_clock)
			a_source->ObserveAt(a_clock());
	}

	void TelemetryDetail::CaptureRenderThread(ThreadIdReader a_reader) noexcept
	{
		if (!Telemetry::EnabledRelaxed())
			return;
		if (a_reader)
			Telemetry::CaptureRenderThread(a_reader());
	}
}
