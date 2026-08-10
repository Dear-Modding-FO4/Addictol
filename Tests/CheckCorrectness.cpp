#include "Harness.h"

#include <vbits.h>
#include <vmmblock.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace
{
	constexpr std::uint32_t oversized_nonnull_exit = 10;

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

		runner.test("realloc with zero size returns nullptr", [&runner] {
			constexpr std::size_t size = 7777;
			void* pointer = voltek::scalable_alloc(size);
			require(pointer != nullptr, "realloc-zero setup allocation failed");

			void* replacement = voltek::scalable_realloc(pointer, 0);
			require(replacement == nullptr, "realloc(ptr, 0) did not return nullptr");
			const auto stale_size = voltek::scalable_msize(pointer);

			std::vector<void*> probes;
			probes.reserve(256);
			bool reused = false;
			for (std::size_t index = 0; index < probes.capacity() && !reused; ++index)
			{
				void* probe = voltek::scalable_alloc(size);
				if (!probe)
					break;
				probes.push_back(probe);
				reused = probe == pointer;
			}
			for (void* probe : probes)
				require(voltek::scalable_free(probe), "realloc-zero probe could not be freed");

			std::ostringstream stream;
			stream << "realloc(ptr, 0) " << (reused ? "freed the original block" : "did not expose the original block as freed")
				   << "; stale msize was " << stale_size;
			runner.info(stream.str());
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
		runner.test("bits_regions rejects page sizes below 65536", [] {
			constexpr std::size_t requested = 32768;
			voltek::core::bits_regions regions;
			regions.resize(requested);
			require(
				regions.count() == 0,
				"bits_regions minimum changed -- page-size reduction for pools 8..8192 may now be possible; re-evaluate the blocks-per-page constants and confirm the AVX2 scan in find_first_free is safe at the new size");
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
