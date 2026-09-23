#include <Core/AdClock.h>
#include <Telemetry/AdTelemetryHub.h>

#include <Windows.h>
#include <Psapi.h>
#include <spdlog/spdlog.h>

#include <charconv>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
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
		inline constexpr uint32_t kCaptureDirectoryError{ 1u << 0 };
		inline constexpr uint32_t kCaptureTelemetryOpenError{ 1u << 1 };
		inline constexpr uint32_t kCaptureSeriesOpenError{ 1u << 2 };
		inline constexpr uint32_t kCaptureMetadataError{ 1u << 3 };
		inline constexpr uint32_t kCaptureTelemetryWriteError{ 1u << 4 };
		inline constexpr uint32_t kCaptureSeriesWriteError{ 1u << 5 };
		inline constexpr uint32_t kCaptureFlushError{ 1u << 6 };
		inline constexpr uint32_t kCaptureWorkerError{ 1u << 7 };

		std::atomic<bool> s_active{ false };
		std::atomic<bool> s_ordinaryEnabled{ false };
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

		bool WriteCsvField(std::ostream& a_stream, std::string_view a_value)
		{
			if (a_value.find_first_of(",\"\r\n") == std::string_view::npos)
			{
				a_stream.write(a_value.data(), a_value.size());
				return a_stream.good();
			}
			a_stream.put('"');
			for (const auto character : a_value)
			{
				if (character == '"')
					a_stream.put('"');
				a_stream.put(character);
			}
			a_stream.put('"');
			return a_stream.good();
		}

		bool WriteJsonString(std::ostream& a_stream, std::string_view a_value)
		{
			a_stream.put('"');
			for (const auto character : a_value)
			{
				switch (character)
				{
				case '"':
					a_stream << "\\\"";
					break;
				case '\\':
					a_stream << "\\\\";
					break;
				case '\b':
					a_stream << "\\b";
					break;
				case '\f':
					a_stream << "\\f";
					break;
				case '\n':
					a_stream << "\\n";
					break;
				case '\r':
					a_stream << "\\r";
					break;
				case '\t':
					a_stream << "\\t";
					break;
				default:
					if (static_cast<unsigned char>(character) < 0x20)
					{
						std::array<char, 7> escape{};
						std::snprintf(
							escape.data(),
							escape.size(),
							"\\u%04x",
							static_cast<unsigned char>(character));
						a_stream << escape.data();
					}
					else
						a_stream.put(character);
					break;
				}
			}
			a_stream.put('"');
			return a_stream.good();
		}

		[[nodiscard]] std::string UtcTimestamp(
			std::chrono::system_clock::time_point a_time,
			bool a_compact)
		{
			const auto raw = std::chrono::system_clock::to_time_t(a_time);
			std::tm utc{};
			if (gmtime_s(&utc, &raw))
				return {};
			std::array<char, 32> buffer{};
			const auto format = a_compact ? "%Y%m%dT%H%M%SZ" : "%Y-%m-%dT%H:%M:%SZ";
			if (!std::strftime(buffer.data(), buffer.size(), format, &utc))
				return {};
			return buffer.data();
		}

		[[nodiscard]] bool CreateUniqueCaptureDirectory(
			const std::filesystem::path& a_root,
			std::string& a_captureId,
			std::filesystem::path& a_directory) noexcept
		{
			try
			{
				std::error_code error;
				std::filesystem::create_directories(a_root, error);
				if (error || !std::filesystem::is_directory(a_root, error))
					return false;
				const auto timestamp =
					UtcTimestamp(std::chrono::system_clock::now(), true);
				if (timestamp.empty())
					return false;
				const auto processId = GetCurrentProcessId();
				for (uint32_t suffix = 0; suffix < 10000; ++suffix)
				{
					std::array<char, 64> id{};
					const auto written = std::snprintf(
						id.data(),
						id.size(),
						"%s-%lu-%04u",
						timestamp.c_str(),
						static_cast<unsigned long>(processId),
						suffix);
					if (written <= 0 || static_cast<size_t>(written) >= id.size())
						return false;
					auto candidate = a_root / id.data();
					error.clear();
					if (std::filesystem::create_directory(candidate, error))
					{
						a_captureId = id.data();
						a_directory = std::move(candidate);
						return true;
					}
					if (error &&
						error != std::make_error_code(std::errc::file_exists))
						return false;
				}
			}
			catch (...)
			{}
			return false;
		}

		void WriteCaptureErrors(std::ostream& a_stream, uint32_t a_errors)
		{
			struct ErrorDescription
			{
				uint32_t flag;
				std::string_view text;
			};
			static constexpr std::array errors{
				ErrorDescription{ kCaptureDirectoryError, "capture_directory" },
				ErrorDescription{ kCaptureTelemetryOpenError, "telemetry_open" },
				ErrorDescription{ kCaptureSeriesOpenError, "series_open" },
				ErrorDescription{ kCaptureMetadataError, "metadata_write" },
				ErrorDescription{ kCaptureTelemetryWriteError, "telemetry_write" },
				ErrorDescription{ kCaptureSeriesWriteError, "series_write" },
				ErrorDescription{ kCaptureFlushError, "flush_or_close" },
				ErrorDescription{ kCaptureWorkerError, "worker_start" }
			};
			a_stream << '[';
			auto first = true;
			for (const auto& error : errors)
			{
				if (!(a_errors & error.flag))
					continue;
				if (!first)
					a_stream << ',';
				first = false;
				(void)WriteJsonString(a_stream, error.text);
			}
			a_stream << ']';
		}

		void AddSaturated(uint64_t& a_target, uint64_t a_value) noexcept
		{
			a_target = a_value >
					std::numeric_limits<uint64_t>::max() - a_target ?
				std::numeric_limits<uint64_t>::max() :
				a_target + a_value;
		}

		void AddProfileCounters(
			OperationProfileCounters& a_target,
			const OperationProfileCounters& a_value) noexcept
		{
			AddSaturated(a_target.sampledAdmissions, a_value.sampledAdmissions);
			AddSaturated(a_target.acceptedRecords, a_value.acceptedRecords);
			AddSaturated(
				a_target.admissionContentionDrops,
				a_value.admissionContentionDrops);
			AddSaturated(
				a_target.publicationContentionDrops,
				a_value.publicationContentionDrops);
			AddSaturated(a_target.capacityDrops, a_value.capacityDrops);
			AddSaturated(a_target.staleTokens, a_value.staleTokens);
			AddSaturated(a_target.invalidResults, a_value.invalidResults);
			AddSaturated(a_target.producerCapacityDrops, a_value.producerCapacityDrops);
			AddSaturated(
				a_target.suppressedOperations,
				a_value.suppressedOperations);
			AddSaturated(
				a_target.unfinishedOperations,
				a_value.unfinishedOperations);
		}
	}

	bool Telemetry::EnabledRelaxed() noexcept
	{
		return s_ordinaryEnabled.load(std::memory_order_relaxed);
	}

	TelemetryHub& Telemetry::Hub() noexcept
	{
		static TelemetryHub hub{ Addictol::GetQpcFrequency() };
		return hub;
	}

	bool Telemetry::ActiveRelaxed() noexcept
	{
		return s_active.load(std::memory_order_relaxed);
	}

	uint32_t Telemetry::RenderThreadIdRelaxed() noexcept
	{
		return s_renderThreadId.load(std::memory_order_relaxed);
	}

	void Telemetry::CaptureRenderThread(uint32_t a_threadId) noexcept
	{
		if (!ActiveRelaxed() || !a_threadId ||
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
		m_qpcFrequency(a_qpcFrequency ? a_qpcFrequency : Addictol::GetQpcFrequency())
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
		if (const auto profile =
			dynamic_cast<OperationProfileSource*>(a_source.get());
			profile && !profile->IsValid())
		{
			spdlog::error("Telemetry: invalid operation profile source rejected");
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
		entry.profileSource =
			dynamic_cast<OperationProfileSource*>(entry.source.get());
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
		TelemetryStartOptions options{};
		options.cadenceMs = a_cadenceMs;
		options.csvPath = std::move(a_csvPath);
		options.seriesCsvPath = std::move(a_seriesCsvPath);
		options.ordinaryTelemetryEnabled = true;
		return Start(std::move(options));
	}

	bool TelemetryHub::Start(TelemetryStartOptions a_options) noexcept
	{
		if (!m_frozen || m_worker.joinable())
			return false;
		m_cadenceMs = std::max(a_options.cadenceMs, 1u);
		try
		{
			m_csvPath = std::move(a_options.csvPath);
			m_seriesCsvPath = std::move(a_options.seriesCsvPath);
			m_productVersion = std::move(a_options.productVersion);
			m_runtime = std::move(a_options.runtime);
			m_buildIdentity = std::move(a_options.buildIdentity);
			m_ordinaryTelemetryEnabled = a_options.ordinaryTelemetryEnabled;
			m_operationProfilingEnabled = !a_options.captureRoot.empty();
			m_captureState.store(
				TelemetryCaptureState::kDisabled,
				std::memory_order_relaxed);
			m_captureErrorFlags.store(0, std::memory_order_relaxed);
			m_captureDirectory.clear();
			m_captureMetadataPath.clear();
			m_captureId.clear();
			m_captureStartedUtc.clear();
			m_captureCompletedUtc.clear();
			ClearIntervals();
			m_stopRequested = false;
			m_workerStartQpc = Addictol::ReadQpc();
			m_captureOriginQpc = m_workerStartQpc;
			(void)PrepareCapture(a_options);
			for (const auto& entry : m_sources)
				entry.source->BeginInterval(m_workerStartQpc);
			s_ordinaryEnabled.store(
				m_ordinaryTelemetryEnabled,
				std::memory_order_release);
			s_active.store(true, std::memory_order_release);
			m_worker = std::thread(&TelemetryHub::Worker, this);
			return true;
		}
		catch (...)
		{
			s_active.store(false, std::memory_order_release);
			s_ordinaryEnabled.store(false, std::memory_order_release);
			FailCapture(kCaptureWorkerError);
			FinalizeCapture();
			spdlog::error("Telemetry: worker could not start");
			return false;
		}
	}

	void TelemetryHub::Stop() noexcept
	{
		s_active.store(false, std::memory_order_relaxed);
		s_ordinaryEnabled.store(false, std::memory_order_relaxed);
		if (m_captureState.load(std::memory_order_acquire) !=
			TelemetryCaptureState::kDisabled)
		{
			for (const auto& entry : m_sources)
			{
				if (entry.profileSource)
					(void)entry.profileSource->CloseCaptureAdmission();
			}
		}
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

	bool TelemetryHub::PrepareCapture(
			const TelemetryStartOptions& a_options) noexcept
		{
		if (a_options.captureRoot.empty())
				return false;

			const ScopedOperationProfileSuppression suppression;
			if (!CreateUniqueCaptureDirectory(
					a_options.captureRoot,
					m_captureId,
					m_captureDirectory))
			{
				m_captureErrorFlags.fetch_or(
					kCaptureDirectoryError,
					std::memory_order_relaxed);
				m_captureState.store(
					TelemetryCaptureState::kIncomplete,
					std::memory_order_release);
				spdlog::error("Telemetry: profiling capture directory could not be created");
				return false;
			}

			m_captureMetadataPath = m_captureDirectory / "metadata.json";
			m_captureStartedUtc =
				UtcTimestamp(std::chrono::system_clock::now(), false);
			m_captureCsv.open(
				m_captureDirectory / "telemetry.csv",
				std::ios::trunc);
			m_captureCsv.imbue(std::locale::classic());
			if (!m_captureCsv ||
				!WriteCsvHeader(m_captureCsv, m_columns) ||
				!(m_captureCsv << std::flush))
			{
				m_captureErrorFlags.fetch_or(
					kCaptureTelemetryOpenError,
					std::memory_order_relaxed);
				m_captureCsv.close();
			}

			m_captureSeriesCsv.open(
				m_captureDirectory / "series.csv",
				std::ios::trunc);
			m_captureSeriesCsv.imbue(std::locale::classic());
			if (!m_captureSeriesCsv ||
				!WriteSeriesCsvHeader(m_captureSeriesCsv) ||
				!(m_captureSeriesCsv << std::flush))
			{
				m_captureErrorFlags.fetch_or(
					kCaptureSeriesOpenError,
					std::memory_order_relaxed);
				m_captureSeriesCsv.close();
			}

			if (!WriteCaptureMetadata(false))
				m_captureErrorFlags.fetch_or(
					kCaptureMetadataError,
					std::memory_order_relaxed);
			if (m_captureErrorFlags.load(std::memory_order_relaxed))
			{
				m_captureState.store(
					TelemetryCaptureState::kIncomplete,
					std::memory_order_release);
				if (m_captureCsv.is_open())
					m_captureCsv.close();
				if (m_captureSeriesCsv.is_open())
					m_captureSeriesCsv.close();
				spdlog::error("Telemetry: profiling capture could not be initialized");
				return false;
			}

			for (const auto& entry : m_sources)
			{
				if (entry.profileSource &&
					!entry.profileSource->StartCapture())
				{
					FailCapture(kCaptureWorkerError);
					return false;
				}
			}
			m_captureState.store(
				TelemetryCaptureState::kActive,
				std::memory_order_release);
			return true;
		}

		bool TelemetryHub::WriteCaptureMetadata(bool a_complete) noexcept
		{
			if (m_captureMetadataPath.empty())
				return false;
			const ScopedOperationProfileSuppression suppression;
			try
			{
				auto temporary = m_captureMetadataPath;
				temporary += ".tmp";
				std::ofstream metadata{ temporary, std::ios::trunc };
				metadata.imbue(std::locale::classic());
				if (!metadata)
					return false;

				const auto errors =
					m_captureErrorFlags.load(std::memory_order_relaxed);
				OperationProfileCounters aggregateCounters{};
				size_t profileSourceCount{ 0 };
				for (const auto& entry : m_sources)
				{
					if (!entry.profileSource)
						continue;
					++profileSourceCount;
					AddProfileCounters(
						aggregateCounters,
						entry.profileSource->Counters());
				}
				metadata << "{\n"
					"  \"schema_version\": 2,\n"
					"  \"capture_id\": ";
				if (!WriteJsonString(metadata, m_captureId))
					return false;
				metadata << ",\n  \"complete\": " <<
					(a_complete ? "true" : "false") <<
					",\n  \"started_utc\": ";
				if (!WriteJsonString(metadata, m_captureStartedUtc))
					return false;
				metadata << ",\n  \"completed_utc\": ";
				if (m_captureCompletedUtc.empty())
					metadata << "null";
				else if (!WriteJsonString(metadata, m_captureCompletedUtc))
					return false;
				metadata << ",\n  \"qpc\": {\"origin\": " <<
					m_captureOriginQpc << ", \"frequency\": " <<
					m_qpcFrequency << "},\n"
					"  \"interval_semantics\": ";
				if (!WriteJsonString(
						metadata,
						"aggregate fields are independently drained and may span adjacent intervals; operation records are coherent sampled tuples"))
					return false;
				metadata << ",\n  \"identity\": {\"product_version\": ";
				if (!WriteJsonString(metadata, m_productVersion))
					return false;
				metadata << ", \"runtime\": ";
				if (!WriteJsonString(metadata, m_runtime))
					return false;
				metadata << ", \"build_identity\": ";
				if (m_buildIdentity.empty())
					metadata << "null";
				else if (!WriteJsonString(metadata, m_buildIdentity))
					return false;
				metadata << "},\n  \"instrumented_run\": " <<
					(profileSourceCount ? "true" : "false") <<
					",\n  \"outputs\": {\"telemetry\": \"telemetry.csv\", "
					"\"series\": \"series.csv\"},\n"
					"  \"profile_sources\": [";
				auto sourceIndex = size_t{ 0 };
				for (const auto& entry : m_sources)
				{
					const auto source = entry.profileSource;
					if (!source)
						continue;
					const auto counters = source->Counters();
					const auto descriptors = source->Descriptors();
					const auto buckets = source->DurationBuckets();
					const auto labels = source->MetadataLabels();
					if (sourceIndex++)
						metadata << ',';
					metadata << "\n    {\"id\": ";
					if (!WriteJsonString(metadata, source->SourceId()))
						return false;
					metadata << ", \"name\": ";
					if (!WriteJsonString(metadata, source->SourceName()))
						return false;
					metadata << ", \"record_capacity\": " <<
						source->RecordCapacity() <<
						", \"publication_shards\": " <<
						source->PublicationShardCount() <<
						", \"counter_lanes\": " << OperationProfileSource::kCounterLaneCount <<
						", \"capture_generation\": " <<
						source->CaptureGeneration() <<
						", \"labels\": [";
					for (size_t index = 0; index < labels.size(); ++index)
					{
						if (index)
							metadata << ',';
						metadata << "{\"key\": ";
						if (!WriteJsonString(metadata, labels[index].key))
							return false;
						metadata << ", \"value\": ";
						if (!WriteJsonString(metadata, labels[index].value))
							return false;
						metadata << '}';
					}
					metadata << "], \"operations\": [";
					for (size_t index = 0; index < descriptors.size(); ++index)
					{
						const auto& descriptor = descriptors[index];
						if (index)
							metadata << ',';
						metadata << "{\"index\": " << index << ", \"series\": ";
						if (!WriteJsonString(metadata, descriptor.series))
							return false;
						metadata << ", \"label\": ";
						if (!WriteJsonString(metadata, descriptor.label))
							return false;
						metadata << ", \"all_operations_metric\": ";
						if (!WriteJsonString(
								metadata,
								descriptor.allOperationsMetricKey))
							return false;
						metadata << ", \"sampling_period\": " <<
							descriptor.samplingPeriod <<
							", \"all_operations\": " <<
							source->AllOperationCount(index) << ", \"result_group\": ";
						if (!WriteJsonString(metadata, descriptor.resultGroup))
							return false;
						metadata << '}';
					}
					metadata << "], \"duration_buckets\": [";
					uint64_t lower{ 0 };
					for (size_t index = 0; index < buckets.size(); ++index)
					{
						if (index)
							metadata << ',';
						metadata << "{\"label\": ";
						if (!WriteJsonString(metadata, buckets[index].label))
							return false;
						metadata << ", \"lower_ns\": " << lower <<
							", \"upper_ns\": ";
						if (buckets[index].upperNanoseconds ==
							std::numeric_limits<uint64_t>::max())
							metadata << "null";
						else
							metadata << buckets[index].upperNanoseconds;
						metadata << '}';
						if (buckets[index].upperNanoseconds !=
							std::numeric_limits<uint64_t>::max())
							lower = buckets[index].upperNanoseconds + 1;
					}
					metadata << "], \"quality\": {"
						"\"sampled_admissions\": " << counters.sampledAdmissions <<
						", \"accepted_records\": " << counters.acceptedRecords <<
						", \"admission_contention_drops\": " <<
							counters.admissionContentionDrops <<
						", \"publication_contention_drops\": " <<
							counters.publicationContentionDrops <<
						", \"capacity_drops\": " << counters.capacityDrops <<
						", \"stale_tokens\": " << counters.staleTokens <<
						", \"invalid_results\": " << counters.invalidResults <<
						", \"producer_capacity_drops\": " << counters.producerCapacityDrops <<
						", \"suppressed_operations\": " <<
							counters.suppressedOperations <<
						", \"unfinished_operations\": " <<
							counters.unfinishedOperations <<
						"}}";
				}
				if (profileSourceCount)
					metadata << '\n' << "  ";
				metadata << "],\n  \"quality\": {"
					"\"sampled_admissions\": " <<
						aggregateCounters.sampledAdmissions <<
					", \"accepted_records\": " <<
						aggregateCounters.acceptedRecords <<
					", \"admission_contention_drops\": " <<
						aggregateCounters.admissionContentionDrops <<
					", \"publication_contention_drops\": " <<
						aggregateCounters.publicationContentionDrops <<
					", \"capacity_drops\": " <<
						aggregateCounters.capacityDrops <<
					", \"stale_tokens\": " <<
						aggregateCounters.staleTokens <<
					", \"invalid_results\": " << aggregateCounters.invalidResults <<
					", \"producer_capacity_drops\": " << aggregateCounters.producerCapacityDrops <<
					", \"suppressed_operations\": " <<
						aggregateCounters.suppressedOperations <<
					", \"unfinished_operations\": " <<
						aggregateCounters.unfinishedOperations <<
					"},\n  \"errors\": ";
				WriteCaptureErrors(metadata, errors);
				metadata << "\n}\n";
				metadata << std::flush;
				if (!metadata.good())
					return false;
				metadata.close();
				if (!metadata.good())
					return false;
				if (!MoveFileExW(
						temporary.c_str(),
						m_captureMetadataPath.c_str(),
						MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				{
					std::error_code removeError;
					std::filesystem::remove(temporary, removeError);
					return false;
				}
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		bool TelemetryHub::WriteCaptureSnapshot(
			const TelemetrySnapshot& a_snapshot) noexcept
		{
			if (m_captureState.load(std::memory_order_acquire) !=
				TelemetryCaptureState::kActive)
				return false;
			if (!m_captureCsv.is_open() ||
				!WriteCsvRow(m_captureCsv, a_snapshot) ||
				!(m_captureCsv << std::flush))
			{
				FailCapture(kCaptureTelemetryWriteError);
				return false;
			}
			if (!m_captureSeriesCsv.is_open() ||
				!WriteSeriesCsvRows(
					m_captureSeriesCsv,
					a_snapshot.qpc,
					a_snapshot.series))
			{
				FailCapture(kCaptureSeriesWriteError);
				return false;
			}
			return true;
		}

		void TelemetryHub::FailCapture(uint32_t a_error) noexcept
		{
			if (m_captureDirectory.empty())
				return;
			m_captureErrorFlags.fetch_or(a_error, std::memory_order_relaxed);
			m_captureState.store(
				TelemetryCaptureState::kIncomplete,
				std::memory_order_release);
			for (const auto& entry : m_sources)
			{
				if (entry.profileSource)
					(void)entry.profileSource->CloseCaptureAdmission();
			}
			if (m_captureCsv.is_open())
				m_captureCsv.close();
			if (m_captureSeriesCsv.is_open())
				m_captureSeriesCsv.close();
		}

		void TelemetryHub::FinalizeCapture() noexcept
		{
			if (m_captureDirectory.empty())
				return;
			const ScopedOperationProfileSuppression suppression;
			if (m_captureCsv.is_open())
			{
				m_captureCsv << std::flush;
				if (!m_captureCsv.good())
					m_captureErrorFlags.fetch_or(
						kCaptureFlushError,
						std::memory_order_relaxed);
				m_captureCsv.close();
				if (m_captureCsv.fail())
					m_captureErrorFlags.fetch_or(
						kCaptureFlushError,
						std::memory_order_relaxed);
			}
			if (m_captureSeriesCsv.is_open())
			{
				m_captureSeriesCsv << std::flush;
				if (!m_captureSeriesCsv.good())
					m_captureErrorFlags.fetch_or(
						kCaptureFlushError,
						std::memory_order_relaxed);
				m_captureSeriesCsv.close();
				if (m_captureSeriesCsv.fail())
					m_captureErrorFlags.fetch_or(
						kCaptureFlushError,
						std::memory_order_relaxed);
			}

			m_captureCompletedUtc =
				UtcTimestamp(std::chrono::system_clock::now(), false);
			OperationProfileCounters counters{};
			for (const auto& entry : m_sources)
			{
				if (entry.profileSource)
					AddProfileCounters(
						counters,
						entry.profileSource->Counters());
			}
			const auto clean =
				!m_captureErrorFlags.load(std::memory_order_relaxed) &&
				!counters.unfinishedOperations;
			if (!WriteCaptureMetadata(clean))
			{
				m_captureErrorFlags.fetch_or(
					kCaptureMetadataError,
					std::memory_order_relaxed);
				m_captureState.store(
					TelemetryCaptureState::kIncomplete,
					std::memory_order_release);
				spdlog::error("Telemetry: profiling metadata finalization failed");
				return;
			}
			m_captureState.store(
				clean ?
					TelemetryCaptureState::kComplete :
					TelemetryCaptureState::kIncomplete,
				std::memory_order_release);
			if (!clean)
				spdlog::error("Telemetry: profiling capture completed incompletely");
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
		OperationProfileCounters profileCounters{};
		for (const auto& entry : m_sources)
		{
			if (entry.profileSource)
				AddProfileCounters(
					profileCounters,
					entry.profileSource->Counters());
		}
		return {
			m_overwritten.load(std::memory_order_relaxed),
			m_skipped.load(std::memory_order_relaxed),
			m_frameRecordOverflows.load(std::memory_order_relaxed),
			profileCounters,
			m_ordinaryTelemetryEnabled,
			m_operationProfilingEnabled
		};
	}

	bool TelemetryHub::CopyCaptureStatus(
		TelemetryCaptureStatus& a_out) const noexcept
	{
		try
		{
			a_out.state = m_captureState.load(std::memory_order_acquire);
			a_out.captureId = m_captureId;
			a_out.directory = m_captureDirectory;
			a_out.errorFlags =
				m_captureErrorFlags.load(std::memory_order_relaxed);
			a_out.instrumented = false;
			for (const auto& entry : m_sources)
			{
				if (entry.profileSource)
				a_out.instrumented = true;
			}
			return a_out.state != TelemetryCaptureState::kDisabled;
		}
		catch (...)
		{
			return false;
		}
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
			if (!sample.calls && !sample.ticks && !sample.bytes)
				continue;
			if (!WriteUnsigned(a_stream, a_qpc))
				return false;
			a_stream << ',';
			if (!WriteCsvField(a_stream, sample.series))
				return false;
			a_stream << ',';
			if (!WriteCsvField(a_stream, sample.bucket))
				return false;
			a_stream << ',';
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
		const ScopedOperationProfileSuppression suppression;
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
			const auto nowQpc = Addictol::ReadQpc();
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
			(void)WriteCaptureSnapshot(snapshot);
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

		if (!m_captureDirectory.empty())
		{
			const auto nowQpc = Addictol::ReadQpc();
			const auto intervalMs = m_qpcFrequency && nowQpc >= previousQpc ?
				static_cast<double>(nowQpc - previousQpc) * 1000.0 /
					static_cast<double>(m_qpcFrequency) : 0.0;
			const auto& snapshot =
				Collect(previousQpc, nowQpc, intervalMs, 0.0);
			(void)WriteCaptureSnapshot(snapshot);
			FinalizeCapture();
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
		const auto telemetryActive = Telemetry::ActiveRelaxed();
		if (!telemetryActive)
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
		if (!Telemetry::ActiveRelaxed())
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
		if (!Telemetry::ActiveRelaxed())
			return;
		if (a_source && a_clock)
			a_source->ObserveAt(a_clock());
	}

	void TelemetryDetail::CaptureRenderThread(ThreadIdReader a_reader) noexcept
	{
		if (!Telemetry::ActiveRelaxed())
			return;
		if (a_reader)
			Telemetry::CaptureRenderThread(a_reader());
	}
}
