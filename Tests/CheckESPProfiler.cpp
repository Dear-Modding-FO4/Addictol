#include "../Addictol/Include/AdProfilerESPCompileFiles.h"
#include "../Addictol/Include/AdProfilerESPSubHooks.h"
#include "Harness.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>
#include <vector>

namespace vmm_tests
{
	void run_esp_profiler_checks(Runner& runner)
	{
		using namespace Addictol::ESPCompileFiles;

		runner.test("ESP CompileFiles signatures reject every one-byte mutation", [] {
			constexpr std::array runtimes{ Runtime::OG, Runtime::NG, Runtime::AE };
			const std::initializer_list<uint8_t> expectedOG{
				0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57,
				0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
			const std::initializer_list<uint8_t> expectedNG{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
			const std::initializer_list<uint8_t> expectedAE{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
			const std::array expected{ expectedOG, expectedNG, expectedAE };

			for (size_t runtimeIndex = 0; runtimeIndex < runtimes.size(); ++runtimeIndex)
			{
				const auto& target = GetTarget(runtimes[runtimeIndex]);
				require(std::ranges::equal(target.signature, expected[runtimeIndex]),
					"CompileFiles signature bytes changed");
				require(Matches(target.signature, target), "exact CompileFiles signature was rejected");
				for (size_t index = 0; index < target.signature.size(); ++index)
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

		runner.test("ESP sub-hook signatures reject every one-byte mutation", [] {
			using Addictol::ESPCompileFiles::Runtime;
			using namespace Addictol::ESPSubHooks;

			const std::array constructExpected{
				std::vector<uint8_t>{
					0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
					0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xDA, 0x45, 0x0F, 0xB6,
					0xE8, 0x4C, 0x8B, 0xF9, 0x45, 0x33, 0xC0, 0x33, 0xD2, 0x48, 0x8B, 0xCB },
				std::vector<uint8_t>{
					0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41,
					0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
					0x8B, 0xDA, 0x45, 0x0F, 0xB6, 0xE0, 0x4C, 0x8B, 0xE9, 0x45, 0x33, 0xC0,
					0x48, 0x8B, 0xCB, 0x33, 0xD2 },
				std::vector<uint8_t>{
					0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41,
					0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
					0x8B, 0xDA, 0x45, 0x0F, 0xB6, 0xE0, 0x4C, 0x8B, 0xE9, 0x45, 0x33, 0xC0,
					0x48, 0x8B, 0xCB, 0x33, 0xD2 }
			};
			const std::array initExpected{
				std::vector<uint8_t>{
					0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57,
					0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0xE8, 0x16, 0x07,
					0xC2, 0x00 },
				std::vector<uint8_t>{
					0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
					0x58, 0x45, 0x33, 0xFF, 0x48, 0x8B, 0xF9, 0x41, 0x8B, 0xEF, 0x44, 0x89,
					0xBC, 0x24, 0x90, 0x00, 0x00, 0x00 },
				std::vector<uint8_t>{
					0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
					0x58, 0x45, 0x33, 0xFF, 0x48, 0x8B, 0xF9, 0x41, 0x8B, 0xEF, 0x44, 0x89,
					0xBC, 0x24, 0x90, 0x00, 0x00, 0x00 }
			};
			constexpr std::array runtimes{ Runtime::OG, Runtime::NG, Runtime::AE };

			for (size_t runtimeIndex = 0; runtimeIndex < runtimes.size(); ++runtimeIndex)
			{
				const auto& construct = GetConstructTarget(runtimes[runtimeIndex]);
				require(
					construct.signatureSize == constructExpected[runtimeIndex].size() &&
						std::equal(
							construct.signature,
							construct.signature + construct.signatureSize,
							constructExpected[runtimeIndex].begin()),
					"ConstructObjectList signature bytes changed");
				for (size_t index = 0; index < construct.signatureSize; ++index)
				{
					auto mutated = constructExpected[runtimeIndex];
					mutated[index] ^= 1;
					require(
						!Matches(std::span{ mutated }, construct),
						"mutated ConstructObjectList signature was accepted");
				}

				const auto& init = GetInitTarget(runtimes[runtimeIndex]);
				require(
					init.signatureSize == initExpected[runtimeIndex].size() &&
						std::equal(
							init.signature,
							init.signature + init.signatureSize,
							initExpected[runtimeIndex].begin()),
					"InitAllForms signature bytes changed");
				for (size_t index = 0; index < init.signatureSize; ++index)
				{
					auto mutated = initExpected[runtimeIndex];
					mutated[index] ^= 1;
					require(
						!Matches(std::span{ mutated }, init),
						"mutated InitAllForms signature was accepted");
				}
			}
		});

		runner.test("ESP sub-hook targets and ABIs are runtime explicit", [] {
			using Addictol::ESPCompileFiles::Runtime;
			using namespace Addictol::ESPSubHooks;

			require(GetConstructTarget(Runtime::OG).id == 1043280, "OG ConstructObjectList id changed");
			require(GetConstructTarget(Runtime::NG).id == 2192326, "NG ConstructObjectList id changed");
			require(GetConstructTarget(Runtime::AE).id == 2192326, "AE ConstructObjectList id changed");
			require(GetInitTarget(Runtime::OG).id == 189223, "OG InitAllForms id changed");
			require(GetInitTarget(Runtime::NG).id == 2192344, "NG InitAllForms id changed");
			require(GetInitTarget(Runtime::AE).id == 2192344, "AE InitAllForms id changed");
			static_assert(std::is_same_v<
				ConstructObjectList,
				bool(__fastcall*)(void*, void*, bool)>);
			static_assert(std::is_same_v<InitAllForms, void(__fastcall*)(void*)>);
		});
	}
}
