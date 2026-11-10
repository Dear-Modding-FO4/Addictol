#include <Modules/AdModuleNavmeshLocationCrash.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	// Both flood passes cache the visited array in r15 across appends that free it.
	namespace navmeshLocationCrashDetail
	{
		struct Pass
		{
			size_t head;    // mov r15, <visited data>
			size_t append;  // index -> element address after AddUninitialized
		};

		inline constexpr size_t kLoopCompare = 0x14;
		inline constexpr std::array<Pass, 2> kPassesOG{ { { 0x1F0, 0x389 }, { 0x740, 0x92C } } };
		inline constexpr std::array<Pass, 2> kPassesNGAE{ { { 0x210, 0x407 }, { 0x7B0, 0x9A7 } } };

		[[nodiscard]] static bool LoopIntact(uintptr_t a_func, const Pass& a_pass) noexcept
		{
			// cmp esi, [r15+r14*8+10h]
			if (!RELEX::Validate(a_func + a_pass.head + kLoopCompare, { 0x43, 0x3B, 0x74, 0xF7, 0x10 }))
				return false;

			if (RELEX::IsRuntimeOG())
			{
				// mov r15, [rbp-80h]
				return RELEX::Validate(a_func + a_pass.head, { 0x4C, 0x8B, 0x7D, 0x80 }) &&
				       // mov eax, eax through test rdx, rdx
				       RELEX::Validate(a_func + a_pass.append, { 0x8B, 0xC0, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8B, 0x45, 0x80,
						   0x48, 0x8D, 0x14, 0xC8, 0x48, 0x85, 0xD2 });
			}

			// mov r15, [rsp+70h]
			return RELEX::Validate(a_func + a_pass.head, { 0x4C, 0x8B, 0x7C, 0x24, 0x70 }) &&
			       // mov rcx, [rsp+70h] through mov [rcx+rdx*8+10h], eax
			       RELEX::Validate(a_func + a_pass.append, { 0x48, 0x8B, 0x4C, 0x24, 0x70, 0x8B, 0xC0, 0x48, 0x8D, 0x14,
					   0x40, 0x8B, 0x45, 0xB8, 0x48, 0x89, 0x3C, 0xD1, 0x66, 0x89, 0x5C, 0xD1, 0x08, 0x89, 0x44, 0xD1, 0x10 });
		}

		static void RefreshCachedArray(uintptr_t a_func, const Pass& a_pass) noexcept
		{
			if (RELEX::IsRuntimeOG())
			{
				// mov r15, [rbp-80h]; lea rdx, [r15+rcx*8] (rax is dead afterwards)
				RELEX::WriteSafe(a_func + a_pass.append + 6, { 0x4C, 0x8B, 0x7D, 0x80, 0x49, 0x8D, 0x14, 0xCF });
				return;
			}

			// mov r15, [rsp+70h]; mov rcx, r15; lea edx, [rax+rax*2] (exact below 2^32/3)
			RELEX::WriteSafe(a_func + a_pass.append, { 0x4C, 0x8B, 0x7C, 0x24, 0x70, 0x49, 0x8B, 0xCF, 0x8D, 0x14, 0x40 });
		}
	}

	ModuleNavmeshLocationCrash::ModuleNavmeshLocationCrash() :
		Module("Navmesh Location Crash", &bFixesNavmeshLocationCrash)
	{}

	bool ModuleNavmeshLocationCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace navmeshLocationCrashDetail;

		const auto func = REL::Relocation<uintptr_t>{ RE::ID::CombatUtilities::FindValidNavmeshLocation }.address();
		const auto& passes = RELEX::IsRuntimeOG() ? kPassesOG : kPassesNGAE;

		for (const auto& pass : passes)
		{
			if (!LoopIntact(func, pass))
			{
				Skip("navmesh flood bytes do not match; unsupported runtime or another plugin patched it"sv);
				return false;
			}
		}

		for (const auto& pass : passes)
			RefreshCachedArray(func, pass);

		return true;
	}
}
