#include "Harness.h"

#include <iostream>
#include <string_view>

namespace
{
	constexpr std::string_view shape_prefix = "--shape-case=";
}

int main(int argc, char** argv)
{
	using namespace vmm_tests;
	std::cout.setf(std::ios::unitbuf);

	if (argc == 2)
	{
		const std::string_view argument{ argv[1] };
		if (argument == "--bench")
		{
			voltek::scalable_memory_manager_initialize();
			const auto benchmark_result = run_benchmarks();
			return benchmark_result == 0 ? run_tracing_benchmarks() : benchmark_result;
		}
		if (argument == "--bench-tracing")
		{
			voltek::scalable_memory_manager_initialize();
			return run_tracing_benchmarks();
		}
		if (argument == "--oversized-case")
			return run_oversized_case();
		if (argument.starts_with(shape_prefix))
			return run_shape_case(argument.substr(shape_prefix.size()));

		std::cerr << "unknown argument: " << argument << '\n';
		return 2;
	}
	if (argc != 1)
	{
		std::cerr << "usage: vmm-tests [--bench|--bench-tracing]\n";
		return 2;
	}

	Runner runner;
	runner.test("memory manager initialization is idempotent", [] {
		voltek::scalable_memory_manager_initialize();
		voltek::scalable_memory_manager_initialize();
	});

	run_correctness_checks(runner);
	run_threading_checks(runner);
	run_shape_checks(runner);
	run_bits_regions_check(runner);
	run_zlib_backend_checks(runner);
	run_zlib_inflate_checks(runner);
	run_libdeflate_checks(runner);
	run_texture_stream_checks(runner);
	run_texture_one_shot_checks(runner);
	run_esp_profiler_checks(runner);
	run_ba2_profiler_checks(runner);
	run_imgui_platform_checks(runner);
	run_escape_freeze_checks(runner);
	run_log_control_checks(runner);

	std::cout << '\n' << runner.tests() - runner.failures() << '/' << runner.tests() << " checks passed\n";
	return runner.failures() == 0 ? 0 : 1;
}
