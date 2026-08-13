#pragma once

#include <Voltek.MemoryManager.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vmm_tests
{
	class Failure final : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	inline void require(bool condition, std::string message)
	{
		if (!condition)
			throw Failure(std::move(message));
	}

	class Runner
	{
	public:
		template <class Function>
		void test(std::string_view name, Function&& function)
		{
			++_tests;
			try
			{
				function();
				std::cout << "[PASS] " << name << '\n';
			}
			catch (const std::exception& error)
			{
				++_failures;
				std::cout << "[FAIL] " << name << ": " << error.what() << '\n';
			}
			catch (...)
			{
				++_failures;
				std::cout << "[FAIL] " << name << ": unknown exception\n";
			}
		}

		void info(std::string_view message) const
		{
			std::cout << "[INFO] " << message << '\n';
		}

		[[nodiscard]] int failures() const
		{
			return _failures;
		}

		[[nodiscard]] int tests() const
		{
			return _tests;
		}

	private:
		int _failures{};
		int _tests{};
	};

	struct AllocationCase
	{
		std::size_t size;
		std::uint8_t pool;
	};

	inline constexpr std::array allocation_cases{
		AllocationCase{ 1, 0 },
		AllocationCase{ 8, 0 },
		AllocationCase{ 9, 1 },
		AllocationCase{ 16, 1 },
		AllocationCase{ 17, 2 },
		AllocationCase{ 32, 2 },
		AllocationCase{ 33, 3 },
		AllocationCase{ 64, 3 },
		AllocationCase{ 65, 4 },
		AllocationCase{ 128, 4 },
		AllocationCase{ 129, 5 },
		AllocationCase{ 256, 5 },
		AllocationCase{ 257, 6 },
		AllocationCase{ 512, 6 },
		AllocationCase{ 513, 7 },
		AllocationCase{ 1024, 7 },
		AllocationCase{ 1025, 8 },
		AllocationCase{ 4096, 8 },
		AllocationCase{ 4097, 9 },
		AllocationCase{ 8192, 9 },
		AllocationCase{ 8193, 10 },
		AllocationCase{ 16384, 10 },
		AllocationCase{ 16385, 11 },
		AllocationCase{ 32768, 11 },
		AllocationCase{ 32769, 12 },
		AllocationCase{ 65536, 12 },
		AllocationCase{ 65537, 13 },
		AllocationCase{ 131072, 13 },
		AllocationCase{ 131073, 0xFF },
		AllocationCase{ 1024 * 1024, 0xFF },
		AllocationCase{ 16 * 1024 * 1024, 0xFF }
	};

	inline std::uint8_t pattern_byte(std::size_t index, std::size_t size, std::uint64_t seed)
	{
		const auto value = seed + size * 17 + index * 131 + (index >> 8) * 29;
		return static_cast<std::uint8_t>(value ^ (value >> 17) ^ (value >> 41));
	}

	inline void fill_pattern(void* pointer, std::size_t size, std::uint64_t seed)
	{
		auto* bytes = static_cast<std::uint8_t*>(pointer);
		for (std::size_t index = 0; index < size; ++index)
			bytes[index] = pattern_byte(index, size, seed);
	}

	inline bool verify_pattern(const void* pointer, std::size_t size, std::uint64_t seed)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(pointer);
		for (std::size_t index = 0; index < size; ++index)
		{
			if (bytes[index] != pattern_byte(index, size, seed))
				return false;
		}
		return true;
	}

	struct ChildProcessResult
	{
		std::uint32_t exit_code;
	};

	ChildProcessResult run_child_process(std::string_view argument);
	void run_correctness_checks(Runner& runner);
	void run_escape_freeze_checks(Runner& runner);
	void run_threading_checks(Runner& runner);
	void run_bits_regions_check(Runner& runner);
	void run_shape_checks(Runner& runner);
	void run_zlib_backend_checks(Runner& runner);
	void run_zlib_inflate_checks(Runner& runner);
	void run_esp_profiler_checks(Runner& runner);
	void run_ba2_profiler_checks(Runner& runner);
	int run_oversized_case();
	int run_shape_case(std::string_view name);
	int run_benchmarks();
	int run_tracing_benchmarks();
}
