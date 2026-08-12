#include "Harness.h"

#include <vmmblock.h>
#include <vmmgeometry.h>

#include <Windows.h>
#include <Psapi.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{
	namespace mm = voltek::memory_manager;

	constexpr std::uint64_t mebibyte = 1024ull * 1024;
	constexpr std::array eager_pool_sizes{
		std::size_t{ 8 },
		std::size_t{ 16 },
		std::size_t{ 32 },
		std::size_t{ 64 }
	};

	// Registry array, bitmaps and the free-block cache reservation a single pool commits alongside its page.
	constexpr std::uint64_t pool_overhead_allowance = 3 * mebibyte;
	constexpr std::uint64_t working_set_bookkeeping_allowance = 1 * mebibyte;

	constexpr std::uint64_t eager_page_bodies =
		mm::page_geometry<mm::block8_t>::body_bytes +
		mm::page_geometry<mm::block16_t>::body_bytes +
		mm::page_geometry<mm::block32_t>::body_bytes +
		mm::page_geometry<mm::block64_t>::body_bytes;

	// The four eagerly created pools commit one page body each plus their own bookkeeping.
	constexpr std::uint64_t eager_pool_ceiling =
		eager_page_bodies + eager_pool_sizes.size() * pool_overhead_allowance;

	// The sample starts after eager pools are primed, so it measures only lazy pools and bookkeeping.
	constexpr std::uint64_t total_retained_ceiling =
		mm::all_page_bodies_bytes - eager_page_bodies + 20 * mebibyte;

	struct MemorySample
	{
		std::uint64_t private_bytes;
		std::uint64_t working_set;
	};

	struct FirstTouchCase
	{
		std::string_view name;
		std::size_t size;
		std::uint64_t ceiling;
	};

	template <class Block>
	constexpr std::uint64_t first_touch_ceiling()
	{
		return mm::page_geometry<Block>::body_bytes + pool_overhead_allowance;
	}

	template <class Block>
	constexpr std::uint64_t eager_zeroing_ceiling()
	{
		constexpr auto body = mm::page_geometry<Block>::body_bytes;
		constexpr auto ceiling = body / 2 + working_set_bookkeeping_allowance;
		static_assert(ceiling < body, "eager-zeroing ceiling must reject a fully touched page body");
		return ceiling;
	}

	constexpr std::array first_touch_cases{
		FirstTouchCase{ "first-touch-128", 128, first_touch_ceiling<mm::block128_t>() },
		FirstTouchCase{ "first-touch-256", 256, first_touch_ceiling<mm::block256_t>() },
		FirstTouchCase{ "first-touch-512", 512, first_touch_ceiling<mm::block512_t>() },
		FirstTouchCase{ "first-touch-1024", 1024, first_touch_ceiling<mm::block1024_t>() },
		FirstTouchCase{ "first-touch-1025", 1025, first_touch_ceiling<mm::block4096_t>() },
		FirstTouchCase{ "first-touch-4097", 4097, first_touch_ceiling<mm::block8192_t>() },
		FirstTouchCase{ "first-touch-8193", 8193, first_touch_ceiling<mm::block16384_t>() },
		FirstTouchCase{ "first-touch-16385", 16385, first_touch_ceiling<mm::block32768_t>() },
		FirstTouchCase{ "first-touch-32769", 32769, first_touch_ceiling<mm::block65536_t>() },
		FirstTouchCase{ "first-touch-65537", 65537, first_touch_ceiling<mm::block131072_t>() },
		// Beyond the largest size class, so this one never reaches a pool.
		FirstTouchCase{ "first-touch-131073", 131073, 4 * mebibyte }
	};

	MemorySample sample_memory()
	{
		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);
		vmm_tests::require(
			GetProcessMemoryInfo(
				GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
				sizeof(counters)) != FALSE,
			"GetProcessMemoryInfo failed");
		return { counters.PrivateUsage, counters.WorkingSetSize };
	}

	std::uint64_t increase(std::uint64_t after, std::uint64_t before)
	{
		return after > before ? after - before : 0;
	}

	double as_mib(std::uint64_t bytes)
	{
		return static_cast<double>(bytes) / static_cast<double>(mebibyte);
	}

	std::string shape_measurement(std::string_view label, std::uint64_t commit, std::uint64_t working_set)
	{
		std::ostringstream stream;
		stream.setf(std::ios::fixed);
		stream.precision(2);
		stream << label << ": commit +" << as_mib(commit) << " MiB, working set +" << as_mib(working_set) << " MiB";
		return stream.str();
	}

	void prime_eager_pools()
	{
		for (const auto size : eager_pool_sizes)
		{
			void* pointer = voltek::scalable_alloc(size);
			vmm_tests::require(pointer != nullptr, "environment could not satisfy eager-pool priming allocation");
			vmm_tests::require(voltek::scalable_free(pointer), "eager-pool priming allocation could not be freed");
		}
	}

	void check_eager_pools()
	{
		const auto before = sample_memory();
		voltek::scalable_memory_manager_initialize();
		prime_eager_pools();
		const auto after = sample_memory();

		const auto commit = increase(after.private_bytes, before.private_bytes);
		const auto working_set = increase(after.working_set, before.working_set);
		std::cout << "[SHAPE] " << shape_measurement("eager-pools", commit, working_set) << '\n';
		vmm_tests::require(commit <= eager_pool_ceiling, "combined eager-pool commit exceeded one page body per eager pool");
	}

	void check_first_touch(const FirstTouchCase& test_case)
	{
		prime_eager_pools();
		const auto before = sample_memory();
		void* pointer = voltek::scalable_alloc(test_case.size);
		vmm_tests::require(pointer != nullptr, "environment could not satisfy first-touch allocation");
		static_cast<volatile std::uint8_t*>(pointer)[0] = 0x5A;
		static_cast<volatile std::uint8_t*>(pointer)[test_case.size - 1] = 0xA5;
		const auto after = sample_memory();

		const auto commit = increase(after.private_bytes, before.private_bytes);
		const auto working_set = increase(after.working_set, before.working_set);
		std::cout << "[SHAPE] " << shape_measurement(test_case.name, commit, working_set) << '\n';
		vmm_tests::require(
			commit <= test_case.ceiling,
			"first-touch commit exceeded its ceiling");
		vmm_tests::require(voltek::scalable_free(pointer), "first-touch allocation could not be freed");
	}

	void check_total_retained()
	{
		prime_eager_pools();
		const auto before = sample_memory();
		std::vector<void*> pointers;
		pointers.reserve(first_touch_cases.size());
		for (const auto& test_case : first_touch_cases)
		{
			void* pointer = voltek::scalable_alloc(test_case.size);
			vmm_tests::require(pointer != nullptr, "environment could not satisfy total-retained allocation");
			static_cast<volatile std::uint8_t*>(pointer)[0] = 0x5A;
			pointers.push_back(pointer);
		}
		const auto after = sample_memory();

		const auto commit = increase(after.private_bytes, before.private_bytes);
		const auto working_set = increase(after.working_set, before.working_set);
		std::cout << "[SHAPE] " << shape_measurement("total-retained", commit, working_set) << '\n';
		vmm_tests::require(commit <= total_retained_ceiling, "total retained commit exceeded one page body per size class plus bookkeeping");

		for (void* pointer : pointers)
			vmm_tests::require(voltek::scalable_free(pointer), "total-retained allocation could not be freed");
	}

	template <class Block>
	void check_working_set(std::size_t size)
	{
		prime_eager_pools();
		vmm_tests::require(EmptyWorkingSet(GetCurrentProcess()) != FALSE, "EmptyWorkingSet failed");
		const auto before = sample_memory();
		void* pointer = voltek::scalable_alloc(size);
		vmm_tests::require(pointer != nullptr, "environment could not satisfy working-set allocation");
		vmm_tests::fill_pattern(pointer, size, 0xEA6E200ull);
		const auto after = sample_memory();

		const auto commit = increase(after.private_bytes, before.private_bytes);
		const auto working_set = increase(after.working_set, before.working_set);
		std::ostringstream label;
		label << "working-set-" << size;
		std::cout << "[SHAPE] " << shape_measurement(label.str(), commit, working_set) << '\n';
		vmm_tests::require(
			working_set <= eager_zeroing_ceiling<Block>(),
			"first allocation eagerly touched its entire backing page");
		vmm_tests::require(voltek::scalable_free(pointer), "working-set allocation could not be freed");
	}

	void check_page_release()
	{
		// 8193 lands in pool16384, whose configured page holds exactly this many blocks.
		constexpr std::size_t size = 8193;
		constexpr std::size_t blocks_per_page = mm::blocks_per_page<mm::block16384_t>;
		constexpr std::uint64_t page_body = mm::page_geometry<mm::block16384_t>::body_bytes;
		constexpr std::size_t pages = 6;
		constexpr std::size_t block_count = blocks_per_page * (pages - 1) + 1;
		std::vector<void*> pointers;
		pointers.reserve(block_count);

		prime_eager_pools();
		const auto before = sample_memory();
		for (std::size_t index = 0; index < block_count; ++index)
		{
			void* pointer = voltek::scalable_alloc(size);
			vmm_tests::require(pointer != nullptr, "environment could not satisfy page-release allocation");
			pointers.push_back(pointer);
		}
		const auto allocated = sample_memory();

		bool all_freed = true;
		for (void* pointer : pointers)
			all_freed = voltek::scalable_free(pointer) && all_freed;
		const auto released = sample_memory();

		const auto rise = increase(allocated.private_bytes, before.private_bytes);
		const auto drop = increase(allocated.private_bytes, released.private_bytes);
		const auto retained = increase(released.private_bytes, before.private_bytes);
		std::ostringstream summary;
		summary.setf(std::ios::fixed);
		summary.precision(2);
		summary << "page-release: commit +" << as_mib(rise) << " MiB, released " << as_mib(drop)
				<< " MiB, retained +" << as_mib(retained) << " MiB";
		std::cout << "[SHAPE] " << summary.str() << '\n';
		vmm_tests::require(all_freed, "page-release free failed");
		vmm_tests::require(rise >= pages * page_body, "allocations did not create every expected backing page");
		vmm_tests::require(drop >= (pages - 1) * page_body, "empty backing pages did not release their commit");
		// Page 0 is retained by design; every later page must be gone, not merely most of them.
		vmm_tests::require(
			retained <= page_body + pool_overhead_allowance,
			"a backing page beyond page 0 stayed committed after every block was freed");
	}

	void run_shape_child(std::string_view name)
	{
		std::string argument = "--shape-case=";
		argument += name;
		const auto result = vmm_tests::run_child_process(argument);
		if (result.exit_code != 0)
		{
			std::ostringstream stream;
			stream << "shape child '" << name << "' exited with code 0x" << std::hex << result.exit_code;
			vmm_tests::require(false, stream.str());
		}
	}
}

namespace vmm_tests
{
	void run_shape_checks(Runner& runner)
	{
		runner.test("combined eager-pool initialization commit", [] { run_shape_child("eager-pools"); });
		for (const auto& test_case : first_touch_cases)
		{
			std::string test_name = "first-touch commit for size ";
			test_name += std::to_string(test_case.size);
			runner.test(test_name, [name = test_case.name] { run_shape_child(name); });
		}

		runner.test("total retained commit", [] { run_shape_child("total-retained"); });
		runner.test("1025-byte eager-zeroing guard", [] { run_shape_child("working-set-1025"); });
		runner.test("4097-byte eager-zeroing guard", [] { run_shape_child("working-set-4097"); });
		runner.test("empty backing pages release commit", [] { run_shape_child("page-release"); });
	}

	int run_shape_case(std::string_view name)
	{
		try
		{
			if (name == "eager-pools")
			{
				check_eager_pools();
				return 0;
			}

			voltek::scalable_memory_manager_initialize();
			for (const auto& test_case : first_touch_cases)
			{
				if (name == test_case.name)
				{
					check_first_touch(test_case);
					return 0;
				}
			}
			if (name == "total-retained")
			{
				check_total_retained();
				return 0;
			}
			if (name == "working-set-1025")
			{
				check_working_set<mm::block4096_t>(1025);
				return 0;
			}
			if (name == "working-set-4097")
			{
				check_working_set<mm::block8192_t>(4097);
				return 0;
			}
			if (name == "page-release")
			{
				check_page_release();
				return 0;
			}

			std::cerr << "[SHAPE FAIL] unknown shape case: " << name << '\n';
			return 2;
		}
		catch (const std::exception& error)
		{
			std::cerr << "[SHAPE FAIL] " << name << ": " << error.what() << '\n';
			return 1;
		}
	}
}
