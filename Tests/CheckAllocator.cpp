#include "Harness.h"

#include <Memory/AdAllocator.h>

#include <array>
#include <atomic>
#include <limits>
#include <cstring>
#include <thread>
#include <windows.h>

namespace
{
	std::atomic<size_t> s_allocationFailureReports{ 0 };
}

void AdAssertMsg(
	[[maybe_unused]] const char* a_sourceFile,
	[[maybe_unused]] int a_sourceLine,
	[[maybe_unused]] const char* a_function,
	[[maybe_unused]] const char* a_formattedMessage,
	...) noexcept
{
	s_allocationFailureReports.fetch_add(1, std::memory_order_relaxed);
}

namespace
{
	struct AlignedCase
	{
		size_t size;
		size_t alignment;
	};

	void require_alignment(const void* a_pointer, size_t a_alignment, std::string_view a_message)
	{
		vmm_tests::require(
			(reinterpret_cast<uintptr_t>(a_pointer) & (a_alignment - 1)) == 0,
			std::string(a_message));
	}
}

namespace vmm_tests
{
	namespace
	{
		template<class Heap>
		void run_heap_checks(Runner& a_runner)
		{
			const std::string name{ Heap::BackendType::kName };

			a_runner.test(name + " heap honors normal and over-aligned requests", [] {
				constexpr std::array cases{
					AlignedCase{ 64, 16 },
					AlignedCase{ 33, 64 },
					AlignedCase{ 131073, 65536 },
					AlignedCase{ 0, 64 }
				};
				auto* heap = Heap::GetSingleton();

				for (const auto& allocation : cases)
				{
					void* pointer = heap->aligned_malloc(allocation.size, allocation.alignment);
					require(pointer != nullptr, "heap allocation failed");
					require_alignment(pointer, allocation.alignment, "heap discarded requested alignment");
					require(heap->msize(pointer) >= allocation.size,
						"heap size query lost the requested size");
					require(heap->aligned_msize(pointer, allocation.alignment) >= allocation.size,
						"aligned size query lost the requested size");
					require(Heap::BackendType::Size(pointer) >= allocation.size + Heap::BackendType::kTailPadding,
						"block lacks the readable tail engine over-reads need");
					heap->aligned_free(pointer);
				}
			});

			a_runner.test(name + " aligned realloc preserves contents and retains failures", [] {
				constexpr size_t oldSize = 4096;
				constexpr size_t newSize = 200000;
				constexpr size_t oldAlignment = 64;
				constexpr size_t newAlignment = 256;
				constexpr uint64_t seed = 0xA110CA7E59ull;
				auto* heap = Heap::GetSingleton();

				void* pointer = heap->malloc(oldSize);
				require(pointer != nullptr, "aligned realloc setup allocation failed");
				fill_pattern(pointer, oldSize, seed);

				void* aligned = heap->aligned_realloc(pointer, oldSize, oldAlignment);
				require(aligned != nullptr, "normal-to-aligned realloc failed");
				require_alignment(aligned, oldAlignment, "normal-to-aligned realloc lost alignment");
				require(verify_pattern(aligned, oldSize, seed),
					"normal-to-aligned realloc corrupted contents");

				void* replacement = heap->aligned_realloc(aligned, newSize, newAlignment);
				require(replacement != nullptr, "aligned realloc failed");
				require_alignment(replacement, newAlignment, "aligned realloc discarded requested alignment");
				require(verify_pattern(replacement, oldSize, seed),
					"aligned realloc did not preserve contents");

				const auto reportsBefore = s_allocationFailureReports.load(std::memory_order_relaxed);
				void* failed = heap->aligned_realloc(
					replacement,
					std::numeric_limits<size_t>::max(),
					newAlignment);
				require(failed == nullptr, "impossible aligned realloc unexpectedly succeeded");
				require(s_allocationFailureReports.load(std::memory_order_relaxed) == reportsBefore + 1,
					"failed allocation was not reported");
				require(heap->aligned_msize(replacement, newAlignment) >= newSize,
					"failed aligned realloc invalidated the original allocation");
				require(
					verify_pattern(replacement, oldSize, seed),
					"failed aligned realloc changed the original contents");
				void* resized = heap->realloc(replacement, oldSize);
				require(resized != nullptr, "ordinary realloc rejected an aligned block");
				require(verify_pattern(resized, oldSize, seed),
					"ordinary realloc corrupted aligned contents");

				void* normal = heap->aligned_realloc(resized, oldSize, 16);
				require(normal != nullptr, "aligned-to-normal realloc failed");
				require(verify_pattern(normal, oldSize, seed),
					"aligned-to-normal realloc corrupted contents");
				heap->free(normal);
			});

			a_runner.test(name + " aligned allocation supports cross-thread handoff", [] {
				constexpr size_t size = 8193;
				constexpr size_t alignment = 128;
				constexpr uint64_t seed = 0xC20557A11ull;
				auto* heap = Heap::GetSingleton();

				void* pointer = heap->aligned_malloc(size, alignment);
				require(pointer != nullptr, "cross-thread setup allocation failed");
				fill_pattern(pointer, size, seed);

				std::exception_ptr failure;
				std::thread consumer([&] {
					try
					{
						require_alignment(pointer, alignment, "cross-thread pointer lost alignment");
						require(heap->aligned_msize(pointer, alignment) >= size,
							"cross-thread size query lost the requested size");
						require(verify_pattern(pointer, size, seed), "cross-thread handoff corrupted contents");
						heap->aligned_free(pointer);
					}
					catch (...)
					{
						failure = std::current_exception();
					}
				});
				consumer.join();
				if (failure)
					std::rethrow_exception(failure);
			});

			// The engine frees blocks allocated before the hooks were installed.
			a_runner.test(name + " heap ignores blocks it does not own", [] {
				auto* heap = Heap::GetSingleton();
				auto* foreign = static_cast<uint8_t*>(VirtualAlloc(nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
				require(foreign != nullptr, "foreign region allocation failed");
				std::memset(foreign, 0xA5, 65536);
				void* sameThreadBlock = heap->malloc(64);
				for (const auto offset : { size_t{ 0 }, size_t{ 16 }, size_t{ 4096 } })
				{
					void* block = foreign + offset;
					require(heap->msize(block) == 0, "size query claimed a foreign block");
					heap->free(block);
					heap->aligned_free(block);
				}
				bool untouched = true;
				for (size_t index = 0; index < 65536; ++index)
					untouched = untouched && foreign[index] == 0xA5;
				require(untouched, "freeing a foreign block wrote into it");
				require(heap->msize(sameThreadBlock) >= 64, "foreign frees disturbed an owned block");
				heap->free(sameThreadBlock);
				VirtualFree(foreign, 0, MEM_RELEASE);
			});
		}
	}

	void run_allocator_checks(Runner& a_runner)
	{
		[&]<class... Backends>(Addictol::HeapBackendList<Backends...>) {
			(run_heap_checks<Addictol::ProxyHeap<Backends>>(a_runner), ...);
		}(Addictol::HeapBackends{});
	}
}
