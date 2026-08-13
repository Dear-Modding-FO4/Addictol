#include "../Addictol/Include/AdProfilerESPCompileFiles.h"
#include "Harness.h"

#include <array>

namespace vmm_tests
{
	void run_esp_profiler_checks(Runner& runner)
	{
		using namespace Addictol::ESPCompileFiles;

		runner.test("ESP CompileFiles signatures reject every one-byte mutation", [] {
			constexpr std::array runtimes{ Runtime::OG, Runtime::NG, Runtime::AE };
			constexpr std::array expected{
				std::array<std::uint8_t, 16>{
					0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57,
					0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 },
				std::array<std::uint8_t, 16>{
					0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
					0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 },
				std::array<std::uint8_t, 16>{
					0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
					0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }
			};

			for (std::size_t runtimeIndex = 0; runtimeIndex < runtimes.size(); ++runtimeIndex)
			{
				const auto& target = GetTarget(runtimes[runtimeIndex]);
				require(target.signature == expected[runtimeIndex], "CompileFiles signature bytes changed");
				require(Matches(target.signature, target), "exact CompileFiles signature was rejected");
				for (std::size_t index = 0; index < target.signature.size(); ++index)
				{
					auto mutated = target.signature;
					mutated[index] ^= 1;
					require(!Matches(mutated, target), "mutated CompileFiles signature was accepted");
				}
			}
		});

		runner.test("ESP CompileFiles target ids are runtime explicit", [] {
			require(GetTarget(Runtime::OG).id == 57137, "OG CompileFiles id changed");
			require(GetTarget(Runtime::NG).id == 2192321, "NG CompileFiles id changed");
			require(GetTarget(Runtime::AE).id == 2192321, "AE CompileFiles id changed");
			require(GetTarget(Runtime::NG).slot == "NG", "NG target slot mislabeled");
			require(GetTarget(Runtime::AE).slot == "AE", "AE target slot mislabeled");
		});
	}
}
