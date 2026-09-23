#include "../Addictol/Include/Telemetry/AdOperationProfile.h"
#include "../Addictol/Include/Telemetry/AdTelemetryHub.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include <Memory/AdProfiledHeap.h>
#include <Zlib/AdZlibOperationProfile.h>
#include <Zlib/AdZlibTelemetry.h>
#include "Harness.h"
#include "ZlibOracle.h"
#include <Zlib/AdZlibBackendRegistry.h>

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <optional>
#include <sstream>
#include <thread>

namespace Addictol::TelemetryTest
{
	struct OperationProfileAccess
	{
		[[nodiscard]] static bool Start(OperationProfileSource& a_source) noexcept
		{
			return a_source.StartCapture();
		}

		[[nodiscard]] static uint64_t Close(OperationProfileSource& a_source) noexcept
		{
			return a_source.CloseCaptureAdmission();
		}

		static void Drain(
			OperationProfileSource& a_source,
			std::span<MetricValue> a_metrics,
			std::span<SeriesSample> a_series) noexcept
		{
			a_source.Drain(a_metrics);
			(void)a_source.DrainSeries(a_series);
		}

		[[nodiscard]] static uint64_t Active(
			const OperationProfileSource& a_source) noexcept
		{
			const auto capture =
				a_source.m_currentCapture.load(std::memory_order_acquire);
			return capture ?
				capture->activeSampled.load(std::memory_order_relaxed) : 0;
		}

		[[nodiscard]] static void* HoldAdmission(
			OperationProfileSource& a_source) noexcept
		{
			const auto capture =
				a_source.m_currentCapture.load(std::memory_order_acquire);
			if (capture)
				capture->activeBegins.fetch_add(
					1, std::memory_order_relaxed);
			return capture;
		}

		static void CompleteHeldAdmission(
			OperationProfileSource& a_source,
			void* a_captureState,
			size_t a_descriptorIndex) noexcept
		{
			auto* capture = static_cast<
				OperationProfileSource::CaptureState*>(a_captureState);
			(void)a_source;
			(void)a_descriptorIndex;
			capture->activeBegins.fetch_sub(1, std::memory_order_seq_cst);
		}

		[[nodiscard]] static bool PinGeneration(OperationProfileSource& a_source, uint64_t a_generation)
		{
			OperationProfileSource::CapturePin pin{
				*a_source.m_currentCapture.load(), a_generation
			};
			return pin.valid;
		}
	};
}

namespace
{
	using namespace Addictol;

	struct TestZlibProfileSource
	{
		inline static OperationProfileSource* source{};
		static OperationProfileSource* Get() noexcept { return source; }
	};

	inline constexpr std::array kTestDescriptors{
		OperationProfileDescriptor{
			"test.operation.first",
			"First operation",
			"profile.test_first_operations",
			1
		},
		OperationProfileDescriptor{
			"test.operation.second",
			"Second operation",
			"profile.test_second_operations",
			1
		}
	};

	inline constexpr std::array kTestBuckets{
		OperationProfileDurationBucket{ 0, "zero" },
		OperationProfileDurationBucket{ 1, "one_ns" },
		OperationProfileDurationBucket{
			std::numeric_limits<uint64_t>::max(),
			"overflow"
		}
	};

	thread_local uint64_t s_profileClock{ 0 };
	std::atomic<uint64_t> s_clockReads{ 0 };

	struct ForwardingHeap
	{
		inline static std::array<uint64_t, 2> storage{};
		inline static void* next{ storage.data() };
		inline static void* input{ nullptr };
		inline static size_t size{ 0 };
		inline static size_t alignment{ 0 };
		inline static uint32_t operation{ 0 };
		inline static uint64_t calls{ 0 };
		static ForwardingHeap* GetSingleton() { static ForwardingHeap heap; return &heap; }
		static void Record(uint32_t a_operation, void* a_input, size_t a_size, size_t a_alignment)
		{
			operation = a_operation; input = a_input; size = a_size; alignment = a_alignment; ++calls;
		}
		void* malloc(size_t a_size) { Record(0, nullptr, a_size, 0); return next; }
		void* aligned_malloc(size_t a_size, size_t a_alignment) { Record(1, nullptr, a_size, a_alignment); return next; }
		void* realloc(void* a_input, size_t a_size) { Record(2, a_input, a_size, 0); return next; }
		void* aligned_realloc(void* a_input, size_t a_size, size_t a_alignment) { Record(3, a_input, a_size, a_alignment); return next; }
		void free(void* a_input) { Record(4, a_input, 0, 0); }
		void aligned_free(void* a_input) { Record(5, a_input, 0, 0); }
		size_t msize(void* a_input) { Record(6, a_input, 0, 0); return 123; }
	};

	uint64_t ReadProfileClock() noexcept
	{
		s_clockReads.fetch_add(1, std::memory_order_relaxed);
		return ++s_profileClock;
	}

	class CountingResource final : public std::pmr::memory_resource
	{
	public:
		[[nodiscard]] size_t Allocations() const noexcept
		{
			return m_allocations.load(std::memory_order_relaxed);
		}

	private:
		void* do_allocate(size_t a_bytes, size_t a_alignment) override
		{
			m_allocations.fetch_add(1, std::memory_order_relaxed);
			return std::pmr::new_delete_resource()->allocate(a_bytes, a_alignment);
		}

		void do_deallocate(
			void* a_pointer,
			size_t a_bytes,
			size_t a_alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(
				a_pointer,
				a_bytes,
				a_alignment);
		}

		[[nodiscard]] bool do_is_equal(
			const std::pmr::memory_resource& a_other) const noexcept override
		{
			return this == &a_other;
		}

		std::atomic<size_t> m_allocations{ 0 };
	};

	[[nodiscard]] std::shared_ptr<OperationProfileSource> MakeProfileSource(
		size_t a_capacity = 1024,
		std::span<const OperationProfileDescriptor> a_descriptors =
			kTestDescriptors,
		std::pmr::memory_resource* a_resource =
			std::pmr::get_default_resource(),
		std::string_view a_sourceId = "test",
		std::string_view a_sourceName = "Test operations",
		uint32_t a_shards = 4)
	{
		OperationProfileConfiguration configuration{};
		configuration.sourceId = a_sourceId;
		configuration.sourceName = a_sourceName;
		configuration.descriptors = a_descriptors;
		configuration.durationBuckets = kTestBuckets;
		configuration.recordCapacity = a_capacity;
		configuration.publicationShardCount = static_cast<uint32_t>(
			(std::min)(a_capacity, static_cast<size_t>(a_shards)));
		configuration.clock = &ReadProfileClock;
		return std::make_shared<OperationProfileSource>(
			configuration,
			1'000'000'000,
			a_resource);
	}

	[[nodiscard]] std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream input{ a_path, std::ios::binary };
		std::ostringstream contents;
		contents << input.rdbuf();
		return contents.str();
	}

	[[nodiscard]] std::filesystem::path UniqueTestPath(std::string_view a_name)
	{
		static std::atomic<uint32_t> sequence{ 0 };
		return std::filesystem::path{ ".Build\\Tests" } /
			(std::string{ a_name } + "-" +
				std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
	}

	void RemoveTestPath(const std::filesystem::path& a_path)
	{
		std::error_code error;
		std::filesystem::remove_all(a_path, error);
	}
}

namespace vmm_tests
{
	void run_operation_profile_checks(Runner& runner)
	{
		runner.test("profiled heap preserves forwarding and classifies realloc without size probes", [] {
			using Heap = ProfiledHeap<ForwardingHeap, HeapProfileSite::CRT>;
			static_assert(std::is_same_v<SelectedProfiledHeap<false, ForwardingHeap, HeapProfileSite::CRT>, ForwardingHeap>);
			require(InstallSelectedProfiledHeap<ForwardingHeap, HeapProfileSite::CRT>(false, []<class Selected> {
				return std::is_same_v<Selected, ForwardingHeap>;
			}), "disabled startup visitor did not select the raw heap");
			TelemetryHub hub;
			require(InitializeHeapOperationProfile(hub, "fake", "visper", "fake"), "heap source registration failed");
			auto* source = HeapOperationProfile();
			require(TelemetryTest::OperationProfileAccess::Start(*source), "heap capture did not start");
			auto* heap = Heap::GetSingleton();
			void* block = ForwardingHeap::storage.data();
			ForwardingHeap::next = block;
			const auto call = [&](uint32_t operation, void* input, size_t size, size_t alignment, auto&& invoke) {
				const auto before = ForwardingHeap::calls;
				invoke();
				require(ForwardingHeap::calls == before + 1 && ForwardingHeap::operation == operation &&
					ForwardingHeap::input == input && ForwardingHeap::size == size && ForwardingHeap::alignment == alignment,
					"heap forwarding changed arguments, pairing, or called an extra heap operation");
			};
			call(0, nullptr, 17, 0, [&] { require(heap->malloc(17) == block, "malloc pointer changed"); });
			call(1, nullptr, 81, 64, [&] { require(heap->aligned_malloc(81, 64) == block, "aligned pointer changed"); });
			call(2, block, 1000, 0, [&] { require(heap->realloc(block, 1000) == block, "in-place pointer changed"); });
			call(3, block, 1001, 128, [&] { require(heap->aligned_realloc(block, 1001, 128) == block, "aligned realloc pointer changed"); });
			call(4, nullptr, 0, 0, [&] { heap->free(nullptr); });
			call(5, block, 0, 0, [&] { heap->aligned_free(block); });
			call(6, block, 0, 0, [&] { require(heap->msize(block) == 123, "msize result changed"); });
			call(2, nullptr, 1000, 0, [&] { require(heap->realloc(nullptr, 1000) == block, "null realloc input changed"); });
			ForwardingHeap::next = nullptr;
			call(2, block, 0, 0, [&] { require(!heap->realloc(block, 0), "zero-size realloc result changed"); });
			call(3, block, 1001, 128, [&] { require(!heap->aligned_realloc(block, 1001, 128), "aligned realloc failure changed"); });
			for (uint32_t repeat = 0; repeat < 8192; ++repeat)
			{
				ForwardingHeap::next = block;
				require(heap->realloc(block, 1000) == block, "in-place realloc changed");
				ForwardingHeap::next = &ForwardingHeap::storage[1];
				require(heap->realloc(block, 1000) == ForwardingHeap::next, "moved realloc changed");
				ForwardingHeap::next = nullptr;
				call(2, block, 1000, 0, [&] { require(!heap->realloc(block, 1000), "failed realloc was replaced"); });
				call(1, nullptr, 81, 64, [&] { require(!heap->aligned_malloc(81, 64), "failed allocation was replaced"); });
			}
			const auto allBefore = source->AllOperationCount(HeapProfileDescriptor(HeapProfileSite::CRT, HeapProfileOperation::Allocate, 17));
			{
				ScopedOperationProfileSuppression suppression;
				(void)heap->malloc(17);
			}
			require(source->Counters()[OperationProfileQuality::kSuppressedOperations] == 1 &&
				source->AllOperationCount(HeapProfileDescriptor(HeapProfileSite::CRT, HeapProfileOperation::Allocate, 17)) == allBefore,
				"profiler-owned heap work recursed into operation recording");
			require(TelemetryTest::OperationProfileAccess::Close(*source) == 0, "heap profile leaked tokens");
			std::vector<MetricValue> metrics(source->Schema().size());
			std::vector<SeriesSample> series(source->SeriesCapacity());
			TelemetryTest::OperationProfileAccess::Drain(*source, metrics, series);
			for (const auto result : { HeapProfileResult::InPlace, HeapProfileResult::Moved, HeapProfileResult::Failed })
			{
				const auto descriptor = HeapProfileDescriptor(HeapProfileSite::CRT, HeapProfileOperation::Reallocate, 1000, result);
				uint64_t count{ 0 };
				for (const auto& sample : series)
				{
					if (sample.series == kHeapProfileDescriptors[descriptor].series)
						count += sample.calls;
				}
				require(count > 0, "realloc outcome was not classified");
			}
			uint64_t failedAllocations{ 0 };
			const auto failedAllocation = HeapProfileDescriptor(
				HeapProfileSite::CRT, HeapProfileOperation::AlignedAllocate, 81, HeapProfileResult::Failed);
			for (const auto& sample : series)
			{
				if (sample.series == kHeapProfileDescriptors[failedAllocation].series)
					failedAllocations += sample.calls;
			}
			require(failedAllocations > 0, "failed allocation was recorded as success");
		});

		runner.test("owned stream profiles complete exactly once on every terminal path", [] {
			using Profile = OwnedZlibStreamProfile<ZlibBackendKind::HybridZlibNg, TestZlibProfileSource>;
			using Owned = OwnedInflate<LibDeflateZlibBackend, ZlibNgDecoder, Profile>;
			const std::vector<uint8_t> payload(4096, 'a');
			const auto compressed = compress_zlib_fixture(payload, 15);
			enum class Terminal { Complete, Reset, Reset2, ResetKeep, End, Error };
			for (const auto terminal : { Terminal::Complete, Terminal::Reset, Terminal::Reset2, Terminal::ResetKeep, Terminal::End, Terminal::Error })
			{
				auto source = MakeProfileSource(16, kZlibProfileDescriptors);
				TestZlibProfileSource::source = source.get();
				require(TelemetryTest::OperationProfileAccess::Start(*source), "zlib profile did not start");
				require(!Telemetry::EnabledRelaxed(), "ordinary telemetry unexpectedly enabled");
				ZlibInflate::Stream stream{};
				require(Owned::Init(&stream) == INFLATE_OK, "profile stream init");
				auto input = compressed;
				if (terminal == Terminal::Error) input.back() ^= 0x80;
				std::vector<uint8_t> output(payload.size());
				stream.next_in = input.data();
				stream.avail_in = static_cast<uint32_t>(input.size());
				stream.next_out = output.data();
				stream.avail_out = terminal == Terminal::Complete || terminal == Terminal::Error ? static_cast<uint32_t>(output.size()) : 1;
				const auto result = Owned::Inflate(&stream, 0);
				const auto bytes = stream.total_out;
				require(result == (terminal == Terminal::Complete ? INFLATE_END :
					terminal == Terminal::Error ? INFLATE_DATA_ERROR : INFLATE_OK), "profile changed inflate result");
				auto* state = ZlibOwnedState::Find(&stream);
				require(state->profileToken.has_value(), "first inflate did not begin a lifetime");
				require(TelemetryTest::OperationProfileAccess::Active(*source) ==
					(terminal == Terminal::Complete || terminal == Terminal::Error ? 0 : 1),
					"lifetime ended before its terminal path");
				if (terminal == Terminal::Reset) require(Owned::Reset(&stream) == INFLATE_OK, "profile reset");
				if (terminal == Terminal::Reset2) require(Owned::Reset2(&stream, 15) == INFLATE_OK, "profile reset2");
				if (terminal == Terminal::ResetKeep) require(Owned::ResetKeep(&stream) == INFLATE_OK, "profile resetKeep");
				if (terminal == Terminal::Reset || terminal == Terminal::Reset2 || terminal == Terminal::ResetKeep)
					require(!state->profileToken, "reset did not rearm lifetime admission");
				if (terminal == Terminal::Complete || terminal == Terminal::Error)
				{
					require(!*state->profileToken, "terminal inflate kept its token");
					(void)Owned::Inflate(&stream, 0);
				}
				require(Owned::End(&stream) == INFLATE_OK, "profile end");
				require(TelemetryTest::OperationProfileAccess::Active(*source) == 0, "terminal path leaked a token");
				require(TelemetryTest::OperationProfileAccess::Close(*source) == 0, "unfinished lifetime");
				std::vector<MetricValue> metrics(source->Schema().size());
				std::vector<SeriesSample> series(source->SeriesCapacity());
				TelemetryTest::OperationProfileAccess::Drain(*source, metrics, series);
				const auto expected = terminal == Terminal::Complete ? "zlib.stream.hybrid_zlib_ng.whole" :
					terminal == Terminal::Error ? "zlib.stream.hybrid_zlib_ng.streaming.decode" : "zlib.stream.hybrid_zlib_ng.buffered";
				uint64_t calls{}, recordedBytes{};
				for (const auto& sample : series)
				{
					calls += sample.calls;
					recordedBytes += sample.bytes;
					if (sample.calls) require(sample.series == expected, "wrong lifetime policy");
				}
				require(calls == 1 && recordedBytes == bytes, "lifetime recorded twice or lost output bytes");
				require(source->Counters()[OperationProfileQuality::kInvalidResults] == 0, "invalid policy result");
				TestZlibProfileSource::source = nullptr;
			}
		});

		runner.test("disabled and unsampled profiling avoid clocks and hot allocations", [] {
			constexpr std::array descriptor{
				OperationProfileDescriptor{
					"test.operation.sampled",
					"Sampled operation",
					"profile.test_sampled_operations",
					4
				}
			};
			CountingResource resource;
			auto source = MakeProfileSource(8, descriptor, &resource);
			require(source->IsValid(), "profile source configuration was rejected");
			s_clockReads.store(0, std::memory_order_relaxed);
			auto disabled = source->Begin(0);
			require(!disabled, "disabled profile source admitted an operation");
			require(
				s_clockReads.load(std::memory_order_relaxed) == 0,
				"disabled profile source read the clock");
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"profile source did not start");
			const auto allocations = resource.Allocations();
			std::optional<OperationProfileToken> sampled;
			for (uint32_t index = 0; index < 64 && !sampled; ++index)
			{
				auto token = source->Begin(0);
				if (token)
					sampled.emplace(std::move(token));
				else
					require(
						s_clockReads.load(std::memory_order_relaxed) == 0,
						"unsampled operation read the clock");
			}
			require(
				sampled.has_value(),
				"deterministic sampling did not admit an operation");
			source->End(std::move(*sampled), 64);
			require(
				s_clockReads.load(std::memory_order_relaxed) == 2,
				"sampled operation did not read exactly two timestamps");
			require(
				resource.Allocations() == allocations,
				"hot profiling record allocated after source construction");
			(void)TelemetryTest::OperationProfileAccess::Close(*source);
		});

		runner.test("interleaved sources and descriptors retain deterministic samples", [] {
			constexpr std::array firstDescriptors{
				OperationProfileDescriptor{
					"test.interleave.first",
					"First interleaved operation",
					"profile.interleave_first_operations",
					5
				},
				OperationProfileDescriptor{
					"test.interleave.second",
					"Second interleaved operation",
					"profile.interleave_second_operations",
					7
				}
			};
			constexpr std::array secondDescriptors{
				OperationProfileDescriptor{
					"test.interleave.other",
					"Other interleaved operation",
					"profile.interleave_other_operations",
					3
				}
			};
			auto first = MakeProfileSource(
				1024,
				firstDescriptors,
				std::pmr::get_default_resource(),
				"interleave_first",
				"First interleaved source");
			auto second = MakeProfileSource(
				1024,
				secondDescriptors,
				std::pmr::get_default_resource(),
				"interleave_second",
				"Second interleaved source");
			require(
				TelemetryTest::OperationProfileAccess::Start(*first) &&
					TelemetryTest::OperationProfileAccess::Start(*second),
				"interleaved profile sources did not start");
			std::array<uint64_t, 3> sampled{};
			for (uint32_t iteration = 0; iteration < 4096; ++iteration)
			{
				auto firstToken = first->Begin(0);
				if (firstToken)
				{
					++sampled[0];
					first->End(std::move(firstToken));
				}
				auto otherToken = second->Begin(0);
				if (otherToken)
				{
					++sampled[1];
					second->End(std::move(otherToken));
				}
				auto secondToken = first->Begin(1);
				if (secondToken)
				{
					++sampled[2];
					first->End(std::move(secondToken));
				}
			}
			require(
				sampled[0] && sampled[1] && sampled[2],
				"interleaving reset or starved a deterministic sampling stream");
			require(
				TelemetryTest::OperationProfileAccess::Close(*first) == 0 &&
					TelemetryTest::OperationProfileAccess::Close(*second) == 0,
				"interleaved sampling left active operations");
		});

		runner.test("profile suppression excludes profiler-owned work only", [] {
			auto source = MakeProfileSource();
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"profile source did not start");
			{
				ScopedOperationProfileSuppression suppression;
				auto token = source->Begin(0);
				require(!token, "suppressed operation returned a token");
			}
			auto token = source->Begin(0);
			require(token, "normal operation remained suppressed");
			source->End(std::move(token), 11);
			const auto counters = source->Counters();
			require(counters[OperationProfileQuality::kSuppressedOperations] == 1,
				"suppressed operation was not counted");
			require(source->AllOperationCount(0) == 1,
				"suppressed profiler work entered application operation totals");
			(void)TelemetryTest::OperationProfileAccess::Close(*source);
		});

		runner.test("bounded profile capacity drops without overwriting live tuples", [] {
			auto source = MakeProfileSource(2, kTestDescriptors,
				std::pmr::get_default_resource(), "test", "Test operations", 1);
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"profile source did not start");
			for (uint64_t bytes : { 11ull, 11ull, 11ull })
			{
				auto token = source->Begin(0);
				require(token, "capacity fixture operation was not admitted");
				source->End(std::move(token), bytes);
			}
			(void)TelemetryTest::OperationProfileAccess::Close(*source);
			std::vector<MetricValue> metrics(source->Schema().size());
			std::vector<SeriesSample> series(source->SeriesCapacity());
			TelemetryTest::OperationProfileAccess::Drain(
				*source,
				metrics,
				series);
			const auto counters = source->Counters();
			require(counters[OperationProfileQuality::kAcceptedRecords] == 2,
				"profile capacity lost an in-capacity record");
			require(counters[OperationProfileQuality::kCapacityDrops] == 1,
				"profile capacity overflow was not counted");
			uint64_t calls{ 0 };
			uint64_t ticks{ 0 };
			uint64_t bytes{ 0 };
			for (const auto& row : series)
			{
				if (row.series != "test.operation.first")
					continue;
				calls += row.calls;
				ticks += row.ticks;
				bytes += row.bytes;
			}
			require(calls == 2 && ticks == 2 && bytes == 22,
				"profile capacity overwrite tore an accepted tuple");
		});

		runner.test("multi-producer profile tuples and losses account exactly", [] {
			constexpr uint32_t threadCount{ 8 };
			constexpr uint32_t iterations{ 10000 };
			auto source = MakeProfileSource(512);
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"profile source did not start");
			std::atomic<bool> collecting{ true };
			std::array<HistogramBucket, kTestDescriptors.size()> totals{};
			std::thread collector([&] {
				std::vector<MetricValue> metrics(source->Schema().size());
				std::vector<SeriesSample> series(source->SeriesCapacity());
				while (collecting.load(std::memory_order_acquire))
				{
					TelemetryTest::OperationProfileAccess::Drain(
						*source,
						metrics,
						series);
					for (const auto& row : series)
					{
						for (size_t descriptor = 0;
							descriptor < kTestDescriptors.size();
							++descriptor)
						{
							if (row.series != kTestDescriptors[descriptor].series)
								continue;
							totals[descriptor].calls += row.calls;
							totals[descriptor].ticks += row.ticks;
							totals[descriptor].bytes += row.bytes;
						}
					}
					std::this_thread::yield();
				}
			});

			std::array<std::thread, threadCount> producers{};
			for (uint32_t thread = 0; thread < threadCount; ++thread)
			{
				producers[thread] = std::thread([&, thread] {
					const auto descriptor = thread % kTestDescriptors.size();
					const auto bytes = descriptor ? 22ull : 11ull;
					for (uint32_t iteration = 0; iteration < iterations; ++iteration)
					{
						auto token = source->Begin(
							static_cast<uint32_t>(descriptor));
						if (token)
							source->End(std::move(token), bytes);
					}
				});
			}
			for (auto& producer : producers)
				producer.join();
			collecting.store(false, std::memory_order_release);
			collector.join();
			require(TelemetryTest::OperationProfileAccess::Close(*source) == 0,
				"completed producers left unfinished operations");
			std::vector<MetricValue> metrics(source->Schema().size());
			std::vector<SeriesSample> series(source->SeriesCapacity());
			TelemetryTest::OperationProfileAccess::Drain(
				*source,
				metrics,
				series);
			for (const auto& row : series)
			{
				for (size_t descriptor = 0;
					descriptor < kTestDescriptors.size();
					++descriptor)
				{
					if (row.series != kTestDescriptors[descriptor].series)
						continue;
					totals[descriptor].calls += row.calls;
					totals[descriptor].ticks += row.ticks;
					totals[descriptor].bytes += row.bytes;
				}
			}

			const auto counters = source->Counters();
			const auto attempted =
				static_cast<uint64_t>(threadCount) * iterations;
			require(
				counters[OperationProfileQuality::kSampledAdmissions] +
					counters[OperationProfileQuality::kAdmissionContentionDrops] == attempted,
				"sampled admission loss was not explicitly accounted");
			require(
				counters[OperationProfileQuality::kAcceptedRecords] +
					counters[OperationProfileQuality::kPublicationContentionDrops] +
					counters[OperationProfileQuality::kCapacityDrops] +
					counters[OperationProfileQuality::kStaleTokens] +
					counters[OperationProfileQuality::kUnfinishedOperations] ==
				counters[OperationProfileQuality::kSampledAdmissions],
				"admitted operation completion loss was not explicitly accounted");
			require(
				totals[0].calls + totals[1].calls ==
					counters[OperationProfileQuality::kAcceptedRecords],
				"collector lost an accepted profile record");
			require(
				totals[0].ticks == totals[0].calls &&
					totals[0].bytes == totals[0].calls * 11,
				"first operation tuple was torn under concurrency");
			require(
				totals[1].ticks == totals[1].calls &&
					totals[1].bytes == totals[1].calls * 22,
				"second operation tuple was torn under concurrency");
		});

		runner.test("capture generations reject stale completions and count unfinished work", [] {
			auto source = MakeProfileSource();
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"first profile capture did not start");
			auto stale = source->Begin(0);
			require(stale, "unfinished fixture operation was not admitted");
			const auto heldAdmission =
				TelemetryTest::OperationProfileAccess::HoldAdmission(*source);
			require(heldAdmission,
				"old capture admission could not be held for restart");
			require(TelemetryTest::OperationProfileAccess::Close(*source) == 1,
				"capture stop did not count unfinished sampled work");
			require(TelemetryTest::OperationProfileAccess::Start(*source),
				"second profile capture did not start");
			TelemetryTest::OperationProfileAccess::CompleteHeldAdmission(
				*source,
				heldAdmission,
				0);
			require(source->AllOperationCount(0) == 0,
				"pending old admission mutated the restarted capture");
			auto current = source->Begin(0);
			require(current, "current capture operation was not admitted");
			require(TelemetryTest::OperationProfileAccess::Active(*source) == 1,
				"current capture active count was not published");
			source->End(std::move(stale), 11);
			const auto counters = source->Counters();
			require(TelemetryTest::OperationProfileAccess::Active(*source) == 1,
				"stale completion decremented the restarted capture");
			require(counters[OperationProfileQuality::kAcceptedRecords] == 0,
				"stale completion published a record after restart");
			source->End(std::move(current), 22);
			require(TelemetryTest::OperationProfileAccess::Close(*source) == 0,
				"stale completion changed the new capture active count");
			for (uint32_t restart = 0; restart < 32; ++restart)
			{
				const auto observedGeneration = source->CaptureGeneration();
				require(TelemetryTest::OperationProfileAccess::Start(*source), "capture state could not be reused");
				require(!TelemetryTest::OperationProfileAccess::PinGeneration(*source, observedGeneration),
					"delayed sampled Begin pinned a recycled generation");
				require(source->AllOperationCount(0) == 0, "old counter lane leaked into a reused capture");
				source->End(source->Begin(0), 1);
				require(source->AllOperationCount(0) == 1, "counter lane did not reset on its owner's next generation");
				require(TelemetryTest::OperationProfileAccess::Close(*source) == 0, "reused capture leaked pins");
			}
		});

		runner.test("two profile sources survive one capture independently", [] {
			constexpr std::array secondDescriptor{
				OperationProfileDescriptor{
					"test.capture.second",
					"Second capture operation",
					"profile.capture_second_operations",
					1
				}
			};
			const auto root = UniqueTestPath("operation-profile-multi-source");
			RemoveTestPath(root);
			TelemetryHub hub{ 1'000'000'000 };
			std::string firstId{ "capture_first" };
			std::string firstName{ "First capture source" };
			std::string firstSeries{ "test.capture.first" };
			std::string firstLabel{ "First capture operation" };
			std::string firstMetric{ "profile.capture_first_operations" };
			const std::array firstDescriptor{
				OperationProfileDescriptor{
					firstSeries,
					firstLabel,
					firstMetric,
					1
				}
			};
			OperationProfileConfiguration firstConfiguration{};
			firstConfiguration.sourceId = firstId;
			firstConfiguration.sourceName = firstName;
			firstConfiguration.descriptors = firstDescriptor;
			firstConfiguration.durationBuckets = kTestBuckets;
			firstConfiguration.recordCapacity = 32;
			firstConfiguration.publicationShardCount = 4;
			firstConfiguration.clock = &ReadProfileClock;
			auto first = std::make_shared<OperationProfileSource>(
				firstConfiguration,
				1'000'000'000);
			firstId.clear();
			firstName.clear();
			firstSeries.clear();
			firstLabel.clear();
			firstMetric.clear();
			auto second = MakeProfileSource(
				48,
				secondDescriptor,
				std::pmr::get_default_resource(),
				"capture_second",
				"Second capture source");
			require(
				hub.Register(first) == TelemetryRegistration::kAccepted &&
					hub.Register(second) == TelemetryRegistration::kAccepted,
				"independent profile source registration failed");
			require(hub.Freeze(2), "multi-source profile hub freeze failed");
			TelemetryStartOptions options{};
			options.cadenceMs = 60000;
			options.captureRoot = root;
			options.productVersion = "test";
			options.runtime = "test";
			require(hub.Start(std::move(options)),
				"multi-source capture worker did not start");
			auto firstToken = first->Begin(0);
			auto secondToken = second->Begin(0);
			require(firstToken && secondToken,
				"one independent profile source did not admit work");
			first->End(std::move(firstToken), 11);
			second->End(std::move(secondToken), 22);
			hub.Stop();

			const auto stats = hub.Stats();
			require(
				stats.operationProfile[OperationProfileQuality::kAcceptedRecords] == 2,
				"hub did not aggregate independent profile counters");
			const auto columns = hub.Columns();
			const auto hasFirstQuality = std::ranges::any_of(
				columns,
				[](const MetricDescriptor& a_descriptor) {
					return a_descriptor.key ==
						"profile.capture_first.accepted_records";
				});
			const auto hasSecondQuality = std::ranges::any_of(
				columns,
				[](const MetricDescriptor& a_descriptor) {
					return a_descriptor.key ==
						"profile.capture_second.accepted_records";
				});
			require(hasFirstQuality && hasSecondQuality,
				"profile quality keys were not source-qualified");

			TelemetryCaptureStatus status{};
			require(hub.CopyCaptureStatus(status) && status.instrumented,
				"multi-source capture status lost instrumentation");
			const auto series = ReadFile(status.directory / "series.csv");
			const auto metadata = ReadFile(status.directory / "metadata.json");
			require(
				series.find("test.capture.first") != std::string::npos &&
					series.find("test.capture.second") != std::string::npos,
				"capture completion lost one source's sampled records");
			require(
				metadata.find("\"id\": \"capture_first\"") !=
						std::string::npos &&
					metadata.find("\"id\": \"capture_second\"") !=
						std::string::npos &&
					metadata.find("\"record_capacity\": 32") !=
						std::string::npos &&
					metadata.find("\"record_capacity\": 48") !=
						std::string::npos &&
					metadata.find("\"accepted_records\": 2") !=
						std::string::npos,
				"capture metadata lost independent source configuration or counters");
			RemoveTestPath(root);
		});

		runner.test("capture stop writes the final partial interval", [] {
			const auto root = UniqueTestPath("operation-profile-final");
			RemoveTestPath(root);
			TelemetryHub hub{ 1'000'000'000 };
			auto source = MakeProfileSource();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"profile source registration failed");
			require(hub.Freeze(2), "profile capture hub freeze failed");
			TelemetryStartOptions options{};
			options.cadenceMs = 60000;
			options.captureRoot = root;
			options.productVersion = "test";
			options.runtime = "test";
			require(hub.Start(std::move(options)),
				"profile capture worker did not start");
			require(Telemetry::ActiveRelaxed(),
				"profiling-only capture did not activate the shared hub");
			require(!Telemetry::EnabledRelaxed(),
				"profiling-only capture activated ordinary telemetry timing");
			auto token = source->Begin(0);
			require(token, "final partial operation was not admitted");
			source->End(std::move(token), 11);
			hub.Stop();
			TelemetryCaptureStatus status{};
			require(hub.CopyCaptureStatus(status),
				"profile capture status was unavailable");
			require(status.state == TelemetryCaptureState::kComplete,
				"fully drained profile capture was not complete");
			const auto series = ReadFile(status.directory / "series.csv");
			const auto metadata = ReadFile(status.directory / "metadata.json");
			require(series.find("test.operation.first") != std::string::npos,
				"capture stop lost the final partial operation");
			require(metadata.find("\"complete\": true") != std::string::npos,
				"clean profile capture manifest stayed incomplete");
			require(
				metadata.find("\"unfinished_operations\": 0") !=
					std::string::npos,
				"clean profile capture reported unfinished work");
			RemoveTestPath(root);
		});

		runner.test("capture output failure stays explicitly incomplete", [] {
			const auto root = UniqueTestPath("operation-profile-output-failure");
			RemoveTestPath(root);
			std::filesystem::create_directories(root.parent_path());
			{
				std::ofstream blockingFile{ root };
				blockingFile << "not a directory";
			}
			TelemetryHub hub{ 1'000'000'000 };
			auto source = MakeProfileSource();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"failed-output profile source registration failed");
			require(hub.Freeze(2), "failed-output profile hub freeze failed");
			TelemetryStartOptions options{};
			options.cadenceMs = 60000;
			options.captureRoot = root;
			options.productVersion = "test";
			options.runtime = "test";
			require(hub.Start(std::move(options)),
				"failed-output telemetry worker did not start");
			auto token = source->Begin(0);
			require(!token,
				"failed capture admitted an operation without persistence");
			hub.Stop();
			TelemetryCaptureStatus status{};
			require(hub.CopyCaptureStatus(status),
				"failed capture status was unavailable");
			require(
				status.state == TelemetryCaptureState::kIncomplete &&
					status.errorFlags != 0,
				"capture output failure was shaped as success");
			RemoveTestPath(root);
		});
	}
}
