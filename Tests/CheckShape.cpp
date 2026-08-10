#include "Harness.h"

#include <Windows.h>
#include <Psapi.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{
	constexpr std::uint64_t mebibyte = 1024ull * 1024;
	constexpr std::array eager_pool_sizes{
		std::size_t{ 8 },
		std::size_t{ 16 },
		std::size_t{ 32 },
		std::size_t{ 64 }
	};

	struct MemorySample
	{
		std::uint64_t private_bytes;
		std::uint64_t working_set;
	};

	struct FirstTouchCase
	{
		std::string_view name;
		std::size_t size;
		std::uint64_t ceiling_mib;
	};

	constexpr std::array first_touch_cases{
		FirstTouchCase{ "first-touch-128", 128, 30 },
		FirstTouchCase{ "first-touch-256", 256, 28 },
		FirstTouchCase{ "first-touch-512", 512, 52 },
		FirstTouchCase{ "first-touch-1024", 1024, 100 },
		FirstTouchCase{ "first-touch-1025", 1025, 390 },
		FirstTouchCase{ "first-touch-4097", 4097, 775 },
		FirstTouchCase{ "first-touch-8193", 8193, 100 },
		FirstTouchCase{ "first-touch-16385", 16385, 200 },
		FirstTouchCase{ "first-touch-32769", 32769, 200 },
		FirstTouchCase{ "first-touch-65537", 65537, 390 },
		FirstTouchCase{ "first-touch-131073", 131073, 4 }
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
		vmm_tests::require(commit <= 50 * mebibyte, "combined eager-pool commit exceeded 50 MiB");
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
			commit <= test_case.ceiling_mib * mebibyte,
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
		vmm_tests::require(commit <= 1700 * mebibyte, "total retained commit exceeded 1700 MiB");

		for (void* pointer : pointers)
			vmm_tests::require(voltek::scalable_free(pointer), "total-retained allocation could not be freed");
	}

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
		vmm_tests::require(working_set <= 8 * mebibyte, "first allocation eagerly touched its entire backing page");
		vmm_tests::require(voltek::scalable_free(pointer), "working-set allocation could not be freed");
	}

	void check_page_release()
	{
		constexpr std::size_t size = 8193;
		constexpr std::size_t blocks_per_page = 4096;
		constexpr std::size_t block_count = blocks_per_page * 5 + 256;
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
		std::ostringstream summary;
		summary.setf(std::ios::fixed);
		summary.precision(2);
		summary << "page-release: commit +" << as_mib(rise) << " MiB, released " << as_mib(drop)
				<< " MiB, retained +" << as_mib(increase(released.private_bytes, before.private_bytes)) << " MiB";
		std::cout << "[SHAPE] " << summary.str() << '\n';
		vmm_tests::require(all_freed, "page-release free failed");
		vmm_tests::require(rise >= 250 * mebibyte, "allocations did not create several backing pages");
		// Page 0 is retained by design, so only later pages must return their commit.
		vmm_tests::require(drop >= 100 * mebibyte, "empty backing pages did not release substantial commit");
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
				check_working_set(1025);
				return 0;
			}
			if (name == "working-set-4097")
			{
				check_working_set(4097);
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
