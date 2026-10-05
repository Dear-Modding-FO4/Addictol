#include "../Addictol/Include/Telemetry/AdOperationProfile.h"
#include "../Addictol/Include/Telemetry/AdTelemetryHub.h"
#include <Memory/AdProfiledHeap.h>
#include "Harness.h"

#include <array>
#include <vector>

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
	};
}

namespace
{
	using namespace Addictol;

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

	// Mirrors a stock engine allocator that allocates through another observed allocator.
	struct NestingHeap
	{
		static NestingHeap* GetSingleton() { static NestingHeap heap; return &heap; }
		void* malloc(size_t a_size) { return ProfiledHeap<ForwardingHeap, HeapProfileSite::SmallBlock>::GetSingleton()->malloc(a_size); }
	};
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
			const auto outer = HeapProfileDescriptor(HeapProfileSite::MemoryManager, HeapProfileOperation::Allocate, 17);
			const auto inner = HeapProfileDescriptor(HeapProfileSite::SmallBlock, HeapProfileOperation::Allocate, 17);
			const auto outerBefore = source->AllOperationCount(outer);
			const auto innerBefore = source->AllOperationCount(inner);
			ForwardingHeap::next = block;
			require(ProfiledHeap<NestingHeap, HeapProfileSite::MemoryManager>::GetSingleton()->malloc(17) == block, "nested heap pointer changed");
			require(source->AllOperationCount(outer) == outerBefore + 1 && source->AllOperationCount(inner) == innerBefore,
				"nested heap call was recorded separately from the outer call");
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

	}
}
