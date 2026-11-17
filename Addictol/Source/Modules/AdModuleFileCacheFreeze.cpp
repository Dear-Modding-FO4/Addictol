#include <Modules/AdModuleFileCacheFreeze.h>
#include <Core/AdUtils.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	// GetEntry snapshots its cold-list cursor once, so a demotion never fixes null.
	namespace fileCacheFreezeDetail
	{
		constexpr uint8_t kSkipHeadWrite = 0x28;

		struct NullCursorGuard : RELEX::Stub
		{
			NullCursorGuard(bool a_og, uintptr_t a_resume, uintptr_t a_exit)
			{
				// OG keeps the streamer in rsi; NG/AE in r14. The cursor is rbx.
				const auto& streamer = a_og ? rsi : r14;
				Xbyak::Label hasCursor;

				test(rbx, rbx);
				jnz(hasCursor);
				JumpTo(a_exit);
				L(hasCursor);
				cmp(rbx, ptr[streamer + 0x220]);
				JumpTo(a_resume);
			}
		};
	}

	ModuleFileCacheFreeze::ModuleFileCacheFreeze() :
		Module("File Cache Freeze", &bFixesFileCacheFreeze)
	{}

	bool ModuleFileCacheFreeze::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace fileCacheFreezeDetail;

		const bool og = RELEX::IsRuntimeOG();
		const auto relocate = [](size_t a_og, size_t a_ngae)
		{
			return REL::Relocation<uintptr_t>{ RE::ID::BSSystemFileStreamer::GetEntry, REL::Offset{ a_og, a_ngae } }.address();
		};

		const auto unlink = relocate(0x263, 0xBA8);  // single-entry unlink of the reused cold victim
		const auto loop = relocate(0x251, 0xB93);    // cursor == head check ending each scan step
		const auto exit = relocate(0x301, 0xC53);    // "no entry" result
		constexpr size_t unlinkTail = 0x2F;                  // head = victim.next, then clear the victim links
		constexpr size_t jmpToTail = 0x10;

		bool valid = false;
		if (og)
		{
			// mov rcx,[rdi+8]; cmp rcx,rdi; jne; mov [rsi+220h],r12; jmp tail
			valid = RELEX::Validate(unlink, { 0x48, 0x8B, 0x4F, 0x08, 0x48, 0x3B, 0xCF, 0x75, 0x09, 0x4C, 0x89, 0xA6,
					0x20, 0x02, 0x00, 0x00, 0xEB, 0x1D }) &&
				// mov rax,[rdi+8]; mov [rsi+220h],rax; mov [rdi+8],r12
				RELEX::Validate(unlink + unlinkTail, { 0x48, 0x8B, 0x47, 0x08, 0x48, 0x89, 0x86, 0x20, 0x02, 0x00,
					0x00, 0x4C, 0x89, 0x67, 0x08 }) &&
				// cmp rbx,[rsi+220h]; jne loop; jmp exit
				RELEX::Validate(loop, { 0x48, 0x3B, 0x9E, 0x20, 0x02, 0x00, 0x00, 0x0F, 0x85, 0x62, 0xFE, 0xFF,
					0xFF, 0xE9, 0x9E, 0x00, 0x00, 0x00 }) &&
				// mov rbx,[rsp+50h]
				RELEX::Validate(exit, { 0x48, 0x8B, 0x5C, 0x24, 0x50 });
		}
		else
		{
			// mov rcx,[rsi+8]; cmp rcx,rsi; jne; mov [r14+220h],r9; jmp tail
			valid = RELEX::Validate(unlink, { 0x48, 0x8B, 0x4E, 0x08, 0x48, 0x3B, 0xCE, 0x75, 0x09, 0x4D, 0x89, 0x8E,
					0x20, 0x02, 0x00, 0x00, 0xEB, 0x1D }) &&
				// mov rax,[rsi+8]; mov [r14+220h],rax; mov [rsi+8],r9
				RELEX::Validate(unlink + unlinkTail, { 0x48, 0x8B, 0x46, 0x08, 0x49, 0x89, 0x86, 0x20, 0x02, 0x00,
					0x00, 0x4C, 0x89, 0x4E, 0x08 }) &&
				// cmp rbx,[r14+220h]; je exit; xor r9d,r9d; jmp scan
				RELEX::Validate(loop, { 0x49, 0x3B, 0x9E, 0x20, 0x02, 0x00, 0x00, 0x0F, 0x84, 0xB3, 0x00, 0x00,
					0x00, 0x45, 0x33, 0xC9, 0xE9, 0x58, 0xFE, 0xFF, 0xFF }) &&
				// xor edi,edi; test rsi,rsi
				RELEX::Validate(exit, { 0x33, 0xFF, 0x48, 0x85, 0xF6 });
		}

		if (!valid)
		{
			Skip("streamer GetEntry bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		NullCursorGuard guard{ og, loop + 7, exit };
		guard.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(loop, trampoline.allocate(guard));
		RELEX::WriteSafeNop(loop + 5, 2);

		// Keeps the head empty instead of re-pointing it at the reused victim.
		RELEX::WriteSafe(unlink + jmpToTail + 1, { kSkipHeadWrite });

		return true;
	}
}
