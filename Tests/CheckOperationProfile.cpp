#include "../Addictol/Include/Telemetry/AdOperationProfile.h"
#include "../Addictol/Include/Telemetry/AdTelemetryHub.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include <Memory/AdProfiledHeap.h>
#include <Zlib/AdZlibOperationProfile.h>
#include <Zlib/AdZlibTelemetry.h>
#include "Harness.h"

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
		size_t aligned_msize(void* a_input, size_t a_alignment) { Record(7, a_input, 0, a_alignment); return 456; }
		void* CheckPtr(void* a_input, size_t a_size) { Record(8, a_input, a_size, 0); return a_input; }
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
		runner.test("result classification rejects foreign groups without losing admission totals", [] {
			constexpr std::array descriptors{
				OperationProfileDescriptor{ "test.request", "request", "profile.test_request", 1, "test.group" },
				OperationProfileDescriptor{ "test.result", "result", "profile.test_result", 1, "test.group" },
				OperationProfileDescriptor{ "test.foreign", "foreign", "profile.test_foreign", 1, "other.group" }
			};
			auto source = MakeProfileSource(16, descriptors);
			require(TelemetryTest::OperationProfileAccess::Start(*source), "capture did not start");
			source->End(source->Begin(0), 42, 1);
			source->End(source->Begin(0), 100, 2);
			source->End(source->Begin(0), 100, UINT32_MAX);
			require(source->Counters().invalidResults == 2 &&
				source->Counters().acceptedRecords == 1 &&
				source->AllOperationCount(0) == 3 && source->AllOperationCount(1) == 0,
				"invalid classification polluted samples or moved admission totals");
			require(TelemetryTest::OperationProfileAccess::Close(*source) == 0, "rejected results leaked active tokens");
			std::vector<MetricValue> metrics(source->Schema().size());
			std::vector<SeriesSample> series(source->SeriesCapacity());
			TelemetryTest::OperationProfileAccess::Drain(*source, metrics, series);
			uint64_t calls{ 0 }, bytes{ 0 };
			for (const auto& sample : series)
			{
				if (sample.calls)
					require(sample.series == "test.result", "result published to admission series");
				calls += sample.calls; bytes += sample.bytes;
			}
			require(calls == 1 && bytes == 42, "classification lost its coherent record");
		});

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
			call(7, block, 0, 256, [&] { require(heap->aligned_msize(block, 256) == 456, "aligned size result changed"); });
			call(8, block, 19, 0, [&] { require(heap->CheckPtr(block, 19) == block, "CheckPtr changed"); });
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
			require(source->Counters().suppressedOperations == 1 &&
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

		runner.test("zlib profile-only dispatch maps primary and every registered fallback", [] {
			auto source = MakeProfileSource(128, kZlibProfileDescriptors);
			require(TelemetryTest::OperationProfileAccess::Start(*source), "zlib profile did not start");
			require(!Telemetry::EnabledRelaxed(), "fixture unexpectedly enabled ordinary telemetry");
			std::vector<std::string> expectedSeries;
			const auto run = [&]<class Backend>() {
				const auto check = [&](const ZlibFallbackReasonEntry& reason,
					uint32_t codecResult, bool hasZlibHeader, std::string_view suffix) {
					const auto outcome = ServeProfiledZlib<Backend, true>(source.get(), [&] {
						ZlibInflateOutcome result{};
						result.fallbackReasonId = ZlibFallbackReasonRegistryId(reason.reason);
						result.primaryCodecResult = codecResult;
						result.hasZlibHeader = hasZlibHeader;
						result.produced = 321;
						result.zlibResult = -3;
						return result;
					});
					require(outcome.zlibResult == -3 && outcome.produced == 321, "profiling changed codec outcome");
					const auto result = ZlibProfileResult(Backend::kind, outcome);
					auto expected = std::string{ "zlib.inflate." } + std::string{ ZlibBackendKindName(Backend::kind) };
					expected += reason.reason == ZlibFallbackReason::None ?
						".primary" : ".fallback." + std::string{ reason.name };
					expected += suffix;
					std::replace(expected.begin(), expected.end(), '-', '_');
					require(result < kZlibProfileDescriptors.size() &&
						kZlibProfileDescriptors[result].series == expected,
						"fallback descriptor mapping failed");
					expectedSeries.push_back(std::move(expected));
				};
				for (const auto& reason : ZLIB_FALLBACK_REASONS)
				{
					if (reason.reason == ZlibFallbackReason::Decode)
					{
						check(reason, ZLIB_CODEC_BAD_DATA, false, ".bad_header");
						check(reason, ZLIB_CODEC_BAD_DATA, true, ".bad_data");
						check(reason, ZLIB_CODEC_INSUFFICIENT_SPACE, true, ".insufficient_space");
						check(reason, ZLIB_CODEC_SHORT_OUTPUT, true, ".short_output");
						check(reason, UINT32_MAX, true, ".other");
					}
					else
						check(reason, UINT32_MAX, true, "");
				}
			};
			run.template operator()<StockZlibBackend>();
			run.template operator()<LibDeflateZlibBackend>();
			uint64_t clockReads{ 0 };
			const auto served = ServeProfiledZlib<StockZlibBackend, true>(source.get(), [&] {
				return TelemetryDetail::ServeTelemetryZlib<StockZlibBackend>(
					nullptr, 0, [](auto*, int32_t) { return -2; },
					[&] { return ++clockReads; }, [] { return 1u; },
					[](const auto&, bool enabled, auto) { require(!enabled, "ordinary recorder enabled in profile-only mode"); });
			});
			require(clockReads == 0 && served.zlibResult == -2, "profile-only mode enabled legacy codec timing");
			(void)TelemetryTest::OperationProfileAccess::Close(*source);
			std::vector<MetricValue> metrics(source->Schema().size());
			std::vector<SeriesSample> series(source->SeriesCapacity());
			TelemetryTest::OperationProfileAccess::Drain(*source, metrics, series);
			uint64_t calls{ 0 }, bytes{ 0 };
			for (const auto& sample : series) { calls += sample.calls; bytes += sample.bytes; }
			require(calls == expectedSeries.size() + 1 &&
				bytes == expectedSeries.size() * 321 &&
				source->Counters().invalidResults == 0, "profile-only zlib records were lost");
			for (const auto& expected : expectedSeries)
			{
				uint64_t resultCalls{ 0 }, resultBytes{ 0 };
				for (const auto& sample : series)
				{
					if (sample.series == expected) { resultCalls += sample.calls; resultBytes += sample.bytes; }
				}
				require(resultCalls == (expected == "zlib.inflate.stock.primary" ? 2 : 1) && resultBytes == 321,
					"zlib fallback outcomes merged into the wrong duration series");
			}
		});
		runner.test("aggregate drains conserve independently exchanged fields", [] {
			constexpr uint64_t producerCount{ 4 };
			constexpr uint64_t iterations{ 50000 };
			Histogram<1> histogram{};
			std::atomic<uint32_t> running{ producerCount };
			std::array<std::thread, producerCount> producers{};
			for (uint64_t producer = 0; producer < producerCount; ++producer)
			{
				producers[producer] = std::thread([&] {
					for (uint64_t index = 0; index < iterations; ++index)
						histogram.Add(0, 1, 2, 3);
					running.fetch_sub(1, std::memory_order_release);
				});
			}

			HistogramBucket total{};
			while (running.load(std::memory_order_acquire))
			{
				const auto drained = histogram.Drain()[0];
				total.calls += drained.calls;
				total.ticks += drained.ticks;
				total.bytes += drained.bytes;
			}
			for (auto& producer : producers)
				producer.join();
			const auto final = histogram.Drain()[0];
			total.calls += final.calls;
			total.ticks += final.ticks;
			total.bytes += final.bytes;
			require(
				total == HistogramBucket{
					producerCount * iterations,
					producerCount * iterations * 2,
					producerCount * iterations * 3
				},
				"aggregate drain lost a contribution across adjacent intervals");
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

		runner.test("profile quality and operation metrics belong to diagnostics", [] {
			auto source = MakeProfileSource();
			for (const auto& descriptor : source->Schema())
			{
				const auto classification = ClassifyTelemetryMetric(descriptor.key);
				require(
					classification.matches == 1 &&
						classification.panel == TelemetryPanel::kOverview,
					"operation profile metric lost its diagnostics owner");
			}
		});

		runner.test("noninstrumented consumer specialization is a true no-op", [] {
			s_clockReads.store(0, std::memory_order_relaxed);
			OperationProfileConsumer<false> consumer;
			auto token = consumer.Begin(0);
			consumer.End(std::move(token), 99);
			require(!token, "noninstrumented consumer produced a token");
			require(
				s_clockReads.load(std::memory_order_relaxed) == 0,
				"noninstrumented consumer read the clock");
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
			require(counters.suppressedOperations == 1,
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
			require(counters.acceptedRecords == 2,
				"profile capacity lost an in-capacity record");
			require(counters.capacityDrops == 1,
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
				counters.sampledAdmissions +
					counters.admissionContentionDrops == attempted,
				"sampled admission loss was not explicitly accounted");
			require(
				counters.acceptedRecords +
					counters.publicationContentionDrops +
					counters.capacityDrops +
					counters.staleTokens +
					counters.unfinishedOperations ==
				counters.sampledAdmissions,
				"admitted operation completion loss was not explicitly accounted");
			require(
				totals[0].calls + totals[1].calls ==
					counters.acceptedRecords,
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

		runner.test("profile percentile estimates stay within truthful buckets", [] {
			const std::array distribution{
				HistogramBucket{ 0, 0, 0 },
				HistogramBucket{ 3, 3, 0 },
				HistogramBucket{ 1, 1, 0 }
			};
			const auto median = EstimateOperationProfilePercentile(
				distribution,
				kTestBuckets,
				0.5);
			require(median.valid && !median.overflow,
				"known profile median was invalid");
			require(
				median.lowerNanoseconds == 1 &&
					median.upperNanoseconds == 1,
				"known profile median escaped its selected bucket");
			const auto maximum = EstimateOperationProfilePercentile(
				distribution,
				kTestBuckets,
				1.0);
			require(maximum.valid && maximum.overflow,
				"profile overflow percentile fabricated a finite bound");
			require(maximum.lowerNanoseconds == 2,
				"profile overflow lower bound changed");
			require(
				!EstimateOperationProfilePercentile(
					std::array<HistogramBucket, 3>{},
					kTestBuckets,
					0.5).valid,
				"empty profile distribution produced a percentile");
			require(
				QpcTicksToNanosecondsSaturated(
					std::numeric_limits<uint64_t>::max(),
					1) == std::numeric_limits<uint64_t>::max(),
				"QPC conversion overflowed instead of saturating");
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
			require(counters.acceptedRecords == 0,
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
				stats.operationProfile.acceptedRecords == 2,
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

		runner.test("profiling capture without consumers is explicit context only", [] {
			const auto root = UniqueTestPath("operation-profile-context-only");
			RemoveTestPath(root);
			TelemetryHub hub{ 1'000'000'000 };
			require(hub.Freeze(2), "context-only profile hub freeze failed");
			TelemetryStartOptions options{};
			options.cadenceMs = 60000;
			options.captureRoot = root;
			options.productVersion = "test";
			options.runtime = "test";
			require(hub.Start(std::move(options)),
				"context-only capture worker did not start");
			hub.Stop();
			TelemetryCaptureStatus status{};
			require(
				hub.CopyCaptureStatus(status) &&
					status.state == TelemetryCaptureState::kComplete &&
					!status.instrumented,
				"context-only capture was not explicitly uninstrumented");
			const auto metadata = ReadFile(status.directory / "metadata.json");
			require(
				metadata.find("\"instrumented_run\": false") !=
						std::string::npos &&
					metadata.find("\"profile_sources\": []") !=
						std::string::npos,
				"context-only metadata implied an installed consumer");
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

		runner.test("unfinished capture cannot produce a clean manifest", [] {
			const auto root = UniqueTestPath("operation-profile-unfinished");
			RemoveTestPath(root);
			TelemetryHub hub{ 1'000'000'000 };
			auto source = MakeProfileSource();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"unfinished profile source registration failed");
			require(hub.Freeze(2), "unfinished profile hub freeze failed");
			TelemetryStartOptions options{};
			options.cadenceMs = 60000;
			options.captureRoot = root;
			options.productVersion = "test";
			options.runtime = "test";
			require(hub.Start(std::move(options)),
				"unfinished profile worker did not start");
			auto token = source->Begin(0);
			require(token, "unfinished operation was not admitted");
			hub.Stop();
			TelemetryCaptureStatus status{};
			require(hub.CopyCaptureStatus(status),
				"unfinished capture status was unavailable");
			require(status.state == TelemetryCaptureState::kIncomplete,
				"unfinished capture was reported complete");
			const auto metadata = ReadFile(status.directory / "metadata.json");
			require(metadata.find("\"complete\": false") != std::string::npos,
				"unfinished capture manifest reported clean success");
			require(
				metadata.find("\"unfinished_operations\": 1") !=
					std::string::npos,
				"unfinished capture manifest lost its active operation");
			source->End(std::move(token), 11);
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
