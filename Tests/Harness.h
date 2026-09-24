#pragma once

#include <Voltek.MemoryManager.h>
#include <vmmclasses.h>

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
		size_t size;
		uint8_t pool;
	};

	inline constexpr auto allocation_cases = [] {
		namespace mm = voltek::memory_manager;
		std::array<AllocationCase, mm::pool_count * 2 + 3> cases{};
		for (size_t index = 0; index < mm::pool_count; ++index)
		{
			cases[index * 2] = { index ? mm::pool_limits[index - 1] + 1 : 1, static_cast<uint8_t>(index) };
			cases[index * 2 + 1] = { mm::pool_limits[index], static_cast<uint8_t>(index) };
		}
		cases[mm::pool_count * 2] = { mm::pool_limit_maximum + 1, 0xFF };
		cases[mm::pool_count * 2 + 1] = { 1024 * 1024, 0xFF };
		cases[mm::pool_count * 2 + 2] = { 16 * 1024 * 1024, 0xFF };
		return cases;
	}();

	inline uint8_t pattern_byte(size_t index, size_t size, uint64_t seed)
	{
		const auto value = seed + size * 17 + index * 131 + (index >> 8) * 29;
		return static_cast<uint8_t>(value ^ (value >> 17) ^ (value >> 41));
	}

	inline void fill_pattern(void* pointer, size_t size, uint64_t seed)
	{
		auto* bytes = static_cast<uint8_t*>(pointer);
		for (size_t index = 0; index < size; ++index)
			bytes[index] = pattern_byte(index, size, seed);
	}

	inline bool verify_pattern(const void* pointer, size_t size, uint64_t seed)
	{
		const auto* bytes = static_cast<const uint8_t*>(pointer);
		for (size_t index = 0; index < size; ++index)
		{
			if (bytes[index] != pattern_byte(index, size, seed))
				return false;
		}
		return true;
	}

	struct ChildProcessResult
	{
		uint32_t exit_code;
	};

	ChildProcessResult run_child_process(std::string_view argument);
	void run_allocator_checks(Runner& runner);
	void run_control_sampler_checks(Runner& runner);
	void run_correctness_checks(Runner& runner);
	void run_escape_freeze_checks(Runner& runner);
	void run_threading_checks(Runner& runner);
	void run_bits_regions_check(Runner& runner);
	void run_shape_checks(Runner& runner);
	void run_zlib_backend_checks(Runner& runner);
	void run_owned_inflate_checks(Runner& runner);
	void run_log_control_checks(Runner& runner);
	void run_menu_checks(Runner& runner);
	void run_setting_registry_checks(Runner& runner);
	void run_shader_reference_effect_lifetime_checks(Runner& runner);
	void run_telemetry_checks(Runner& runner);
	void run_operation_profile_checks(Runner& runner);
	int run_oversized_case();
	int run_shape_case(std::string_view name);
	int run_benchmarks(std::string_view a_backend);
	int run_profile_benchmarks();
}
