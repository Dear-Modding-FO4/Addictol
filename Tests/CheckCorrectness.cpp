#include "Harness.h"

#include <vbits.h>
#include <vmmblock.h>
#include <vmmgeometry.h>
#include <vmmmain.h>
#include <vmmpage.h>

#include <algorithm>
#include <thread>
#include <barrier>
#include <atomic>
#include <array>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <type_traits>
#include <vector>
#include <windows.h>

namespace
{
	constexpr std::uint32_t oversized_nonnull_exit = 10;
	constexpr std::size_t mebibyte = 1024u * 1024;

	namespace mm = voltek::memory_manager;

	static_assert([] {
		for (const auto& item : mm::class_geometries)
			if (item.count < mm::minimum_blocks_per_page ||
				(item.body_bytes > mm::page_body_target_bytes && item.count != mm::minimum_blocks_per_page) ||
				item.retained_pages != 1)
				return false;
		return true;
	}());

	constexpr std::size_t summed_page_bodies()
	{
		std::size_t total = 0;
		for (const auto& item : mm::class_geometries)
			total += item.body_bytes;
		return total;
	}

	static_assert(summed_page_bodies() == mm::all_page_bodies_bytes,
		"the configured table and the geometry policy disagree");
	// These bounds guard the over-read; the runtime test below covers only the scalar handover.
	static_assert(voltek::core::_internal::complete_simd_word_count(2048, 2048) == 32);
	static_assert(voltek::core::_internal::complete_simd_word_count(2305, 2048) == 32);
	static_assert(voltek::core::_internal::complete_simd_word_count(4096, 2048) == 64);
	static_assert(voltek::core::_internal::complete_simd_word_count(2048, 1024) == 32);
	static_assert(voltek::core::_internal::complete_simd_word_count(2305, 1024) == 32);
	static_assert(voltek::core::_internal::complete_simd_word_count(4096, 1024) == 64);

	std::string size_message(std::string_view message, std::size_t size)
	{
		std::ostringstream stream;
		stream << message << " at size " << size;
		return stream.str();
	}

	bool verify_original_pattern(const void* pointer, std::size_t length, std::size_t original_size, std::uint64_t seed)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(pointer);
		for (std::size_t index = 0; index < length; ++index)
		{
			if (bytes[index] != vmm_tests::pattern_byte(index, original_size, seed))
				return false;
		}
		return true;
	}

	void check_reallocation(std::size_t old_size, std::size_t new_size)
	{
		const auto seed = 0xA110CA7Eull ^ old_size ^ (new_size << 1);
		void* pointer = voltek::scalable_alloc(old_size);
		vmm_tests::require(pointer != nullptr, size_message("initial realloc allocation failed", old_size));
		vmm_tests::fill_pattern(pointer, old_size, seed);

		void* replacement = voltek::scalable_realloc(pointer, new_size);
		vmm_tests::require(replacement != nullptr, size_message("realloc failed", new_size));
		vmm_tests::require(
			verify_original_pattern(replacement, std::min(old_size, new_size), old_size, seed),
			"realloc did not preserve the original contents");
		vmm_tests::require(voltek::scalable_free(replacement), "realloc result could not be freed");
	}
}

namespace vmm_tests
{
	void run_correctness_checks(Runner& runner)
	{
		runner.test("pool lookup selects the smallest fitting class", [] {
			for (size_t size = 1; size <= mm::pool_limit_maximum; ++size)
			{
				const auto found = std::find_if(mm::pool_limits.begin(), mm::pool_limits.end(),
					[size](size_t limit) { return size <= limit; });
				require(mm::pool_class_of(size) == static_cast<size_t>(found - mm::pool_limits.begin()),
					"pool lookup did not select the smallest fitting class");
			}
		});

		runner.test("mapper skips padding and reuses the lowest free slot", [] {
			constexpr size_t count = 2051;
			constexpr size_t stride = voltek::core::region::commit_granularity;
			auto* base = static_cast<char*>(VirtualAlloc(nullptr, count * stride, MEM_RESERVE, PAGE_READWRITE));
			require(base != nullptr, "mapper reservation failed");
			voltek::core::mapper mapper;
			mapper.assign(base, stride, count);
			for (size_t index = 0; index < count; ++index)
				require(mapper.allocate(stride) == base + index * stride, "mapper did not select the lowest free slot");
			require(mapper.allocate(stride) == nullptr && mapper.committed_bytes() == count * stride,
				"mapper handed out padding or miscounted committed bytes");
			mapper.release(base + 2049 * stride);
			mapper.release(base + 7 * stride);
			require(mapper.allocate(stride) == base + 7 * stride && mapper.allocate(stride) == base + 2049 * stride,
				"mapper did not reuse released slots in address order");
			for (size_t index = 0; index < count; ++index)
				mapper.release(base + index * stride);
			require(mapper.committed_bytes() == 0, "mapper retained decommitted bytes");
			VirtualFree(base, 0, MEM_RELEASE);
		});

		runner.test("large slots reuse committed memory within a shared budget", [] {
			const auto sample = [] {
				std::array<voltek::scalable_class_stats, mm::pool_count + 1> classes{};
				voltek::scalable_get_class_stats(classes.data(), classes.size());
				return classes.back();
			};
			constexpr size_t small = 300 * 1024;
			constexpr size_t grown = 400 * 1024;
			auto* other_slot = voltek::scalable_alloc(600 * 1024);
			require(other_slot != nullptr, "large reuse anchor allocation failed");
			auto* pointer = voltek::scalable_alloc(small);
			require(pointer != nullptr, "large reuse fixture allocation failed");
			std::memset(pointer, 0xA5, small);
			const auto committed = sample().committed_bytes;
			require(voltek::scalable_free(pointer), "large reuse fixture free failed");
			require(sample().committed_bytes == committed, "large free did not retain its committed slot");
			require(!voltek::scalable_free(pointer) && voltek::scalable_msize(pointer) == 0 &&
				voltek::scalable_realloc(pointer, small) == nullptr, "retained large block was treated as live");
			auto* reused = voltek::scalable_alloc(small);
			require(reused == pointer && sample().committed_bytes == committed,
				"large reuse changed its pointer or committed bytes");
			std::atomic<size_t> accepted{ 0 };
			std::barrier start{ 2 };
			const auto release = [&] {
				start.arrive_and_wait();
				accepted.fetch_add(voltek::scalable_free(pointer) ? 1 : 0);
			};
			std::thread other{ release };
			release();
			other.join();
			require(accepted.load() == 1, "large concurrent double free changed ownership twice");
			auto* larger = static_cast<uint8_t*>(voltek::scalable_alloc(grown));
			require(larger == pointer && sample().committed_bytes == committed + grown - small,
				"retained slot growth did not commit only its missing tail");
			require(std::all_of(larger, larger + small, [](uint8_t value) { return value == 0xA5; }),
				"retained slot growth discarded its committed prefix");
			larger[grown - 1] = 0x5A;
			const auto grown_commit = sample().committed_bytes;
			require(voltek::scalable_free(larger), "grown large block free failed");
			auto* smaller = static_cast<uint8_t*>(voltek::scalable_calloc(1, small));
			require(smaller == pointer && sample().committed_bytes == grown_commit - (grown - small),
				"smaller reuse did not trim its excess committed pages");
			constexpr size_t small_commit = (small + sizeof(mm::block_base) + voltek::core::region::commit_granularity - 1) &
				~(voltek::core::region::commit_granularity - 1);
			MEMORY_BASIC_INFORMATION tail{};
			require(VirtualQuery(reinterpret_cast<char*>(mm::get_block_handle_from_ptr(smaller)) + small_commit,
				&tail, sizeof(tail)) == sizeof(tail) && tail.State == MEM_RESERVE,
				"smaller reuse left its excess tail committed");
			require(std::all_of(smaller, smaller + small, [](uint8_t value) { return value == 0; }),
				"calloc did not clear reused large memory");
			require(voltek::scalable_free(smaller), "smaller large block free failed");
			require(sample().committed_bytes == committed, "smaller slot release restored its old committed extent");
			require(voltek::scalable_free(other_slot), "large reuse anchor free failed");

			std::array<void*, 10> first{}, second{};
			for (size_t index = 0; index < first.size(); ++index)
			{
				first[index] = voltek::scalable_alloc(4 * mebibyte);
				second[index] = voltek::scalable_alloc(8 * mebibyte);
				require(first[index] && second[index], "retention budget fixture allocation failed");
			}
			const auto all_live = sample().committed_bytes;
			for (auto* block : first)
				require(voltek::scalable_free(block), "first retention class free failed");
			constexpr size_t first_extent = 4 * mebibyte + voltek::core::region::commit_granularity;
			require(sample().committed_bytes <= all_live - 2 * first_extent,
				"one large class retained more than eight slots");
			for (auto* block : second)
				require(voltek::scalable_free(block), "second retention class free failed");
			const auto retained = sample();
			require(retained.live_blocks == 0 && retained.requested_bytes == 0 &&
				retained.committed_bytes <= mm::large_retention_budget_bytes,
				"large classes exceeded their shared retention budget");

			auto* oversized = voltek::scalable_alloc(mm::large_retention_budget_bytes + mebibyte);
			require(oversized != nullptr, "over-budget retention fixture allocation failed");
			require(voltek::scalable_free(oversized), "over-budget retention fixture free failed");
			require(sample().committed_bytes == retained.committed_bytes,
				"a slot larger than the entire budget was retained");
		});

		runner.test("thread exit returns cached blocks to the pool", [] {
			constexpr size_t size = 3072;
			std::array<void*, 2> released{};
			std::array<void*, 2> reused{};
			bool released_ok = true;
			bool reused_ok = true;
			bool late_free_ok = false;
			std::thread producer{ [&] {
				struct late_free
				{
					bool& success;
					~late_free()
					{
						auto* keep = voltek::scalable_alloc(16);
						auto* pointer = voltek::scalable_alloc(16);
						success = keep && pointer && voltek::scalable_free(pointer);
						if (success)
							success = !(mm::block_lifecycle::load(mm::get_block_handle_from_ptr(pointer)) & mm::flag_block_cached);
						if (keep)
							success = voltek::scalable_free(keep) && success;
					}
				};
				thread_local late_free after_cache{ late_free_ok };
				(void)after_cache;
				for (auto& pointer : released)
					pointer = voltek::scalable_alloc(size);
				for (auto* pointer : released)
					released_ok = pointer && voltek::scalable_free(pointer) && released_ok;
			} };
			producer.join();
			require(late_free_ok, "late TLS destructor reused a dead cache");
			std::thread consumer{ [&] {
				for (auto& pointer : reused)
					pointer = voltek::scalable_alloc(size);
				for (auto* pointer : reused)
					reused_ok = pointer && voltek::scalable_free(pointer) && reused_ok;
			} };
			consumer.join();
			require(released_ok && reused_ok, "thread-exit fixture allocation or free failed");
			std::sort(released.begin(), released.end());
			std::sort(reused.begin(), reused.end());
			require(released == reused, "exited thread kept its cached blocks");
		});

		runner.test("cached frees reject stale access", [] {
			for (const size_t size : { 1, 100, 4096 })
			{
				auto* pointer = voltek::scalable_alloc(size);
				require(pointer && voltek::scalable_msize(pointer) == size, "cache allocation lost the requested size");
				require(voltek::scalable_free(pointer), "cache free failed");
				require(voltek::scalable_msize(pointer) == 0 &&
					voltek::scalable_realloc(pointer, size) == nullptr &&
					voltek::scalable_aligned_realloc(pointer, size, 32) == nullptr,
					"cached block was treated as live");
				require(!voltek::scalable_free(pointer), "cached double free was accepted");
			}
		});

		runner.test("all size-class boundaries round trip", [] {
			for (const auto& allocation : allocation_cases)
			{
				void* pointer = voltek::scalable_alloc(allocation.size);
				require(pointer != nullptr, size_message("allocation failed", allocation.size));
				fill_pattern(pointer, allocation.size, 0xC001D00Dull);
				require(
					verify_pattern(pointer, allocation.size, 0xC001D00Dull),
					size_message("data corruption", allocation.size));
				require(voltek::scalable_free(pointer), size_message("free failed", allocation.size));
			}
		});

		runner.test("msize and pool dispatch match requests", [] {
			for (const auto& allocation : allocation_cases)
			{
				void* pointer = voltek::scalable_alloc(allocation.size);
				require(pointer != nullptr, size_message("allocation failed", allocation.size));

				const auto measured = voltek::scalable_msize(pointer);
				require(measured >= allocation.size, size_message("msize was smaller than requested", allocation.size));
				require(measured == allocation.size, size_message("msize did not preserve the requested size", allocation.size));

				if (allocation.pool == 0xFF)
				{
					require(
						voltek::memory_manager::is_used_default_ptr(pointer),
						size_message("request unexpectedly used a pool", allocation.size));
				}
				else
				{
					require(
						voltek::memory_manager::get_pool_id_from_ptr(pointer) == allocation.pool,
						size_message("request used the wrong size class", allocation.size));
				}

				require(voltek::scalable_free(pointer), size_message("free failed", allocation.size));
			}
		});

		runner.test("allocations are at least 16-byte aligned", [] {
			for (const auto& allocation : allocation_cases)
			{
				void* pointer = voltek::scalable_alloc(allocation.size);
				require(pointer != nullptr, size_message("allocation failed", allocation.size));
				require(
					(reinterpret_cast<std::uintptr_t>(pointer) & 0xF) == 0,
					size_message("allocation was not 16-byte aligned", allocation.size));
				require(voltek::scalable_free(pointer), size_message("free failed", allocation.size));
			}
		});

		runner.test("zero-size allocation returns a constant address", [] {
			void* first = voltek::scalable_alloc(0);
			void* second = voltek::scalable_alloc(0);
			require(first != nullptr, "zero-size allocation returned nullptr");
			require(first == second, "zero-size allocations returned different addresses");
		});

		runner.test("msize rejects foreign pointers", [] {
			int stack_value = 42;
			require(voltek::scalable_msize(&stack_value) == 0, "msize accepted a stack address");

			void* crt_pointer = std::malloc(4096);
			require(crt_pointer != nullptr, "CRT malloc failed");
			const auto measured = voltek::scalable_msize(crt_pointer);
			std::free(crt_pointer);
			require(measured == 0, "msize accepted a CRT allocation");
		});

		runner.test("free nullptr is safe", [&runner] {
			const bool result = voltek::scalable_free(nullptr);
			runner.info(std::string("scalable_free(nullptr) returned ") + (result ? "true" : "false"));
		});

		runner.test("realloc preserves data across allocator boundaries", [] {
			check_reallocation(1024, 4096);
			check_reallocation(4096, 1024);
			check_reallocation(100, 200000);
			check_reallocation(200000, 100);
		});

		runner.test("calloc zeroes the entire request", [] {
			constexpr std::size_t count = 257;
			constexpr std::size_t element_size = 4097;
			constexpr std::size_t size = count * element_size;
			auto* dirty = voltek::scalable_alloc(size);
			require(dirty != nullptr, "calloc dirty-slot fixture allocation failed");
			std::memset(dirty, 0xA5, size);
			require(voltek::scalable_free(dirty), "calloc dirty-slot fixture free failed");
			void* pointer = voltek::scalable_calloc(count, element_size);
			require(pointer != nullptr, "calloc failed");

			const auto* bytes = static_cast<const std::uint8_t*>(pointer);
			require(
				std::all_of(bytes, bytes + size, [](std::uint8_t value) { return value == 0; }),
				"calloc left non-zero bytes");
			require(voltek::scalable_free(pointer), "calloc result could not be freed");
		});

		runner.test("recalloc preserves old data and zeroes growth", [] {
			constexpr std::size_t old_size = 777;
			constexpr std::size_t new_size = 10000;
			constexpr std::uint64_t seed = 0x5EC0110Cull;

			void* pointer = voltek::scalable_alloc(old_size);
			require(pointer != nullptr, "recalloc setup allocation failed");
			fill_pattern(pointer, old_size, seed);

			void* replacement = voltek::scalable_recalloc(pointer, 1, new_size);
			require(replacement != nullptr, "recalloc failed");
			require(
				verify_original_pattern(replacement, old_size, old_size, seed),
				"recalloc did not preserve the original region");

			const auto* bytes = static_cast<const std::uint8_t*>(replacement);
			require(
				std::all_of(bytes + old_size, bytes + new_size, [](std::uint8_t value) { return value == 0; }),
				"recalloc did not zero the newly added region");
			require(voltek::scalable_free(replacement), "recalloc result could not be freed");
		});

		runner.test("realloc with zero size returns nullptr", [] {
			constexpr std::size_t size = 7777;
			void* pointer = voltek::scalable_alloc(size);
			require(pointer != nullptr, "realloc-zero setup allocation failed");

			void* replacement = voltek::scalable_realloc(pointer, 0);
			require(replacement == nullptr, "realloc(ptr, 0) did not return nullptr");
		});

		runner.test("allocation larger than 4 GiB is characterized", [] {
			const auto result = run_child_process("--oversized-case");
			if (result.exit_code == 0 || result.exit_code == oversized_nonnull_exit)
				return;

			std::ostringstream stream;
			stream << "oversized allocation child crashed or failed with exit code 0x" << std::hex << result.exit_code;
			require(false, stream.str());
		});
	}

	void run_bits_regions_check(Runner& runner)
	{
		runner.test("partial SIMD chunks hand off to the scalar scan", [] {
			const auto check_tail = [](size_t set_index) {
				constexpr size_t count = 2305;
				voltek::core::bits map;
				map.resize(count);
				map.all_unset();
				require(map.set(set_index), "tail test could not set its target bit");

				size_t index = SIZE_MAX;
				require(map.find_first_set_bit(index), "tail test lost its only set bit");
				require(index == set_index, "tail test returned the wrong bit index");
				require(index < count, "tail test returned an index outside the bitmap");
			};

			const bool original_avx2 = voltek::core::avx2_supported;
			const bool original_sse41 = voltek::core::sse41_supported;
			struct RestoreFeatures
			{
				bool avx2;
				bool sse41;
				~RestoreFeatures()
				{
					voltek::core::avx2_supported = avx2;
					voltek::core::sse41_supported = sse41;
				}
			} restore{ original_avx2, original_sse41 };

			check_tail(2200);
			check_tail(2304);
			if (original_sse41)
			{
				voltek::core::avx2_supported = false;
				voltek::core::sse41_supported = true;
				check_tail(2200);
				check_tail(2304);
			}
		});

		runner.test("bits scan reports no index outside a configured page", [] {
			for (const auto& item : mm::class_geometries)
			{
				voltek::core::bits map;
				map.resize(item.count);
				require(map.count() == item.count, size_message("bits refused the configured count", item.limit));

				map.all_set();
				for (std::size_t remaining = item.count; remaining > 0; --remaining)
				{
					std::size_t index = SIZE_MAX;
					require(map.find_first_set_bit(index), size_message("bits lost a free block", item.limit));
					require(index < item.count, size_message("bits reported an index past the page", item.limit));
					require(map.unset(index), size_message("bits reported an already busy block", item.limit));
				}

				std::size_t index = 0;
				require(!map.find_first_set_bit(index), size_message("an exhausted page still reported a free block", item.limit));
			}
		});

		runner.test("pool page maps report no index outside the pool", [] {
			voltek::core::bits_regions regions;
			constexpr std::size_t count = 64 * 1024;
			regions.resize(count);
			require(regions.count() == count, "bits_regions refused the pool page count");
			regions.all_set();
			for (std::size_t remaining = count; remaining > 0; --remaining)
			{
				std::size_t index = SIZE_MAX;
				require(regions.find_first_set_bit(index), "bits_regions lost a free block");
				require(index < count, "bits_regions reported an index past the page");
				require(regions.unset(index), "bits_regions reported an already busy block");
			}
		});

		// The engine hands foreign blocks to free and msize; a lookalike header must not be trusted or touched.
		runner.test("ownership rejects a forged header outside the reservation", [] {
			auto* page = static_cast<char*>(VirtualAlloc(nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			require(page != nullptr, "forged header page allocation failed");
			auto* header = mm::create_default_block(reinterpret_cast<mm::block_base*>(page), 64);
			DWORD previous = 0;
			require(VirtualProtect(page, 65536, PAGE_READONLY, &previous) != 0, "forged header page could not be protected");
			void* forged = mm::get_ptr_from_block_handle(header);
			require(voltek::scalable_msize(forged) == 0, "size query trusted a forged header");
			require(!voltek::scalable_free(forged), "free accepted a forged header");
			require(voltek::scalable_realloc(forged, 128) == nullptr, "realloc accepted a forged header");
			VirtualFree(page, 0, MEM_RELEASE);
		});

		runner.test("cache refills never publish a live intermediate block", [] {
			constexpr size_t size = 100;
			constexpr auto geometry = mm::class_geometries[mm::pool_class_of(size)];
			mm::global_memory_manager->flush_thread_cache();
			void* keep = voltek::scalable_alloc(size);
			require(keep != nullptr, "refill race anchor allocation failed");
			mm::global_memory_manager->flush_thread_cache();
			std::barrier phase{ 2 };
			std::atomic<bool> refilled{ false };
			bool done = false;
			void* target = nullptr;
			void* stolen = nullptr;
			std::thread duplicate_free{ [&] {
				for (;;)
				{
					phase.arrive_and_wait();
					if (done)
						break;
					bool accepted = false;
					while (!refilled.load(std::memory_order_acquire))
					{
						if (voltek::scalable_free(target))
						{
							accepted = true;
							break;
						}
					}
					phase.arrive_and_wait();
					stolen = accepted ? voltek::scalable_alloc(size) : nullptr;
					phase.arrive_and_wait();
				}
			} };
			bool unique = true;
			bool fixture = true;
			bool rejected = true;
			for (size_t round = 0; round < 20000 && unique && fixture && rejected; ++round)
			{
				std::array<void*, geometry.cache_cap + 1> blocks{};
				for (size_t index = 0; index < geometry.cache_cap; ++index)
				{
					blocks[index] = voltek::scalable_alloc(size);
					fixture &= blocks[index] && mm::get_page_id_from_ptr(blocks[index]) == mm::get_page_id_from_ptr(keep);
				}
				target = blocks[geometry.cache_cap - 1];
				for (size_t index = 0; index < geometry.cache_cap; ++index)
					fixture &= voltek::scalable_free(blocks[index]);
				mm::global_memory_manager->flush_thread_cache();
				refilled.store(false, std::memory_order_relaxed);
				phase.arrive_and_wait();
				// The head enters the bottom of the refill bin, never a live allocation while free spins.
				blocks[0] = voltek::scalable_alloc(size);
				refilled.store(true, std::memory_order_release);
				phase.arrive_and_wait();
				phase.arrive_and_wait();
				fixture &= blocks[0] && blocks[0] != target;
				for (size_t index = 1; index < geometry.cache_cap; ++index)
					blocks[index] = voltek::scalable_alloc(size);
				blocks.back() = stolen;
				rejected &= stolen == nullptr;
				std::sort(blocks.begin(), blocks.end(), std::less<void*>{});
				const auto first = std::find_if(blocks.begin(), blocks.end(), [](void* block) { return block != nullptr; });
				unique &= std::adjacent_find(first, blocks.end()) == blocks.end();
				const auto end = std::unique(first, blocks.end());
				for (auto it = first; it != end; ++it)
					voltek::scalable_free(*it);
				mm::global_memory_manager->flush_thread_cache();
			}
			done = true;
			phase.arrive_and_wait();
			duplicate_free.join();
			voltek::scalable_free(keep);
			require(fixture, "refill race did not keep the target on its anchored page and out of live allocations");
			require(unique && rejected, "a refill block was acquired by the duplicate-free thread");
		});

		// The game double-frees; the second free must not put a live block on the free list twice.
		runner.test("a block freed twice is handed out once", [] {
			void* keep = voltek::scalable_alloc(100);
			for (int round = 0; round < 1000; ++round)
			{
				void* block = voltek::scalable_alloc(100);
				std::atomic<int> accepted{ 0 };
				std::barrier start{ 2 };
				const auto release = [&] {
					start.arrive_and_wait();
					accepted.fetch_add(voltek::scalable_free(block) ? 1 : 0);
				};
				std::thread other{ release };
				release();
				other.join();
				require(accepted.load() == 1, "a concurrent double free was accepted twice or not at all");
				void* first = voltek::scalable_alloc(100);
				void* second = voltek::scalable_alloc(100);
				require(first != second, "a double-freed block was handed out twice");
				voltek::scalable_free(first);
				voltek::scalable_free(second);
			}
			voltek::scalable_free(keep);
		});

		// A use-after-free write can overwrite a free block's link; allocation must not follow it.
		runner.test("a corrupted free-list link is not followed", [] {
			for (const size_t size : { 100, 8193 })
			{
				void* keep = nullptr;
				void* b = nullptr;
				bool setup = false;
				std::thread producer{ [&] {
					keep = voltek::scalable_alloc(size);
					void* a = voltek::scalable_alloc(size);
					b = voltek::scalable_alloc(size);
					setup = keep && a && b && voltek::scalable_free(a) && voltek::scalable_free(b);
				} };
				producer.join();
				require(setup && (mm::block_lifecycle::load(mm::get_block_handle_from_ptr(b)) & mm::flag_block_free),
					"setup did not flush the corrupted block to the page free list");
				const auto& geometry = mm::class_geometries[mm::pool_class_of(size)];
				// Stay inside committed memory so removing the guard yields a named failure, not an access violation.
				*static_cast<uint32_t*>(b) = static_cast<uint32_t>(geometry.count - 1);
				std::vector<void*> blocks;
				std::thread consumer{ [&] {
					for (size_t index = 0; index < (std::max)(size_t{ 4 }, geometry.cache_batch); ++index)
						blocks.push_back(voltek::scalable_alloc(size));
				} };
				consumer.join();
				const bool skipped = std::find(blocks.begin(), blocks.end(), b) == blocks.end();
				for (auto* block : blocks)
				{
					require(block != nullptr && voltek::scalable_msize(block) == size, "allocation after a corrupted link failed");
					std::memset(block, 0x5A, size);
				}
				std::sort(blocks.begin(), blocks.end());
				require(std::adjacent_find(blocks.begin(), blocks.end()) == blocks.end(), "a corrupted link handed out one block twice");
				for (auto* block : blocks)
					voltek::scalable_free(block);
				voltek::scalable_free(keep);
				require(skipped, "a block with a corrupted page link was consumed");
			}
		});

		// A pool that grew and shrank must reuse the pages it kept, not create one per allocation.
		runner.test("a shrunken pool reuses retained pages", [] {
			constexpr std::size_t size = 8193;
			constexpr std::size_t pool = mm::pool_class_of(size);
			constexpr std::size_t blocks = mm::class_geometries[pool].count * 8;
			voltek::scalable_enable_statistics();
			std::vector<void*> grown;
			for (std::size_t index = 0; index < blocks; ++index)
				grown.push_back(voltek::scalable_alloc(size));
			for (auto* block : grown)
				voltek::scalable_free(block);
			std::array<voltek::scalable_class_stats, mm::pool_count + 1> before{};
			voltek::scalable_get_class_stats(before.data(), before.size());
			for (int round = 0; round < 1000; ++round)
				voltek::scalable_free(voltek::scalable_alloc(size));
			std::array<voltek::scalable_class_stats, mm::pool_count + 1> after{};
			voltek::scalable_get_class_stats(after.data(), after.size());
			require(after[pool].pages_created == before[pool].pages_created,
				"alloc and free of one block kept creating pages after the pool shrank");
		});

		// The phase-by-phase evaluation reads these counters; drift would misattribute memory.
		runner.test("class statistics return to baseline after blocks are freed", [] {
			const auto sample = [] {
				std::array<voltek::scalable_class_stats, mm::pool_count + 1> classes{};
				require(voltek::scalable_get_class_stats(classes.data(), classes.size()) == classes.size(),
					"class statistics did not cover every class");
				return classes;
			};
			constexpr std::size_t pooled = mm::pool_class_of(100);
			constexpr std::size_t uncached = mm::pool_class_of(8192);
			constexpr std::size_t large = mm::pool_count;
			voltek::scalable_enable_statistics();
			mm::global_memory_manager->flush_thread_cache();
			const auto before = sample();
			std::vector<void*> blocks;
			for (std::size_t index = 0; index < 1000; ++index)
				blocks.push_back(voltek::scalable_alloc(100));
			for (std::size_t index = 0; index < 10; ++index)
				require(voltek::scalable_realloc(blocks[index], 110) == blocks[index], "in-class realloc moved the block");
			void* big = voltek::scalable_alloc(300000);
			void* direct = voltek::scalable_alloc(8192);
			require(big && direct, "statistics fixture allocation failed");
			const auto during = sample();
			require(during[pooled].live_blocks == before[pooled].live_blocks + 1000 &&
				during[pooled].requested_bytes == before[pooled].requested_bytes + 1000 * 100 + 10 * 10,
				"pooled class statistics missed live blocks or resized bytes");
			require(during[large].live_blocks == before[large].live_blocks + 1 &&
				during[large].requested_bytes == before[large].requested_bytes + 300000 + sizeof(mm::block_base),
				"large block statistics missed a live block");
			const auto batch = mm::class_geometries[pooled].cache_cap;
			require(during[pooled].held_blocks == before[pooled].held_blocks + (1000 + batch - 1) / batch * batch &&
				during[large].held_blocks == during[large].live_blocks,
				"held statistics did not include cache refills and large live blocks");
			require(during[uncached].held_blocks == before[uncached].held_blocks + 1,
				"held statistics missed an uncached allocation");
			for (auto* block : blocks)
				require(voltek::scalable_free(block), "pooled block free failed");
			require(voltek::scalable_free(big), "large block free failed");
			require(voltek::scalable_free(direct), "uncached block free failed");
			const auto after = sample();
			require(after[pooled].held_blocks > before[pooled].held_blocks &&
				after[pooled].held_blocks <= before[pooled].held_blocks + mm::class_geometries[pooled].cache_cap &&
				after[large].held_blocks == before[large].held_blocks,
				"held statistics did not retain only cached blocks");
			require(after[uncached].held_blocks == before[uncached].held_blocks,
				"held statistics retained an uncached release");
			mm::global_memory_manager->flush_thread_cache();
			require(sample()[pooled].held_blocks == before[pooled].held_blocks,
				"cache flush did not return held statistics to baseline");
			require(after[pooled].live_blocks == before[pooled].live_blocks &&
				after[pooled].requested_bytes == before[pooled].requested_bytes &&
				after[large].live_blocks == before[large].live_blocks &&
				after[large].requested_bytes == before[large].requested_bytes &&
				after[large].committed_bytes <= mm::large_retention_budget_bytes,
				"class statistics did not return to baseline");
		});
	}

	int run_oversized_case()
	{
		constexpr std::size_t oversized = 4ull * 1024 * 1024 * 1024 + 1;
		voltek::scalable_memory_manager_initialize();
		void* pointer = voltek::scalable_alloc(oversized);
		if (!pointer)
		{
			std::cout << "[OVERSIZED] allocation of " << oversized << " bytes returned nullptr\n";
			return 0;
		}

		const auto measured = voltek::scalable_msize(pointer);
		const bool freed = voltek::scalable_free(pointer);
		std::cout << "[INFO] oversized allocation returned non-null; msize=" << measured
				  << ", free=" << (freed ? "true" : "false") << '\n';
		return oversized_nonnull_exit;
	}
}
