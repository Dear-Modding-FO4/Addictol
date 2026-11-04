#include <Modules/AdModuleCreatedRefBaseCrash.h>
#include <Core/AdUtils.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	// A null original base now takes the engine's own mismatch rebuild branch.
	namespace createdRefBaseCrashDetail
	{
		struct BaseGuard : Xbyak::CodeGenerator
		{
			BaseGuard(uintptr_t a_mismatch, uintptr_t a_match)
			{
				Xbyak::Label mismatch;
				Xbyak::Label match;
				Xbyak::Label toMismatch;
				Xbyak::Label toMatch;

				test(rax, rax);
				jz(toMismatch);
				cmp(dword[rax + 0x14], ecx);
				jz(toMatch);
				L(toMismatch);
				jmp(ptr[rip + mismatch]);
				L(toMatch);
				jmp(ptr[rip + match]);
				L(mismatch);
				dq(a_mismatch);
				L(match);
				dq(a_match);
			}
		};
	}

	ModuleCreatedRefBaseCrash::ModuleCreatedRefBaseCrash() :
		Module("Created Ref Base Crash", &bFixesCreatedRefBaseCrash)
	{}

	bool ModuleCreatedRefBaseCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace createdRefBaseCrashDetail;

		// The compare sits 0x283 into the load function on OG, NG and AE.
		const auto site = REL::Relocation<uintptr_t>{ RE::ID::BGSSaveLoadGame::PrepareReferenceLoad, 0x283 }.address();

		// cmp [rax+14h], ecx; jz +19h; mov rdx, r14; mov rcx, r13; call
		if (!RELEX::Validate(site, { 0x39, 0x48, 0x14, 0x74, 0x19, 0x49, 0x8B, 0xD6, 0x49, 0x8B, 0xCD, 0xE8 }))
		{
			Skip("created-reference base compare bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		BaseGuard guard{ site + 5, site + 5 + 0x19 };
		guard.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(site, trampoline.allocate(guard));

		return true;
	}
}
