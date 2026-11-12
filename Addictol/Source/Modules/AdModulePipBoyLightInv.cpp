#include <Modules/AdModulePipBoyLightInv.h>
#include <Core/AdUtils.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	namespace pipBoyDetail
	{
		struct Patch : RELEX::Stub
		{
			explicit Patch(std::uintptr_t a_dest, std::uintptr_t a_rtn, std::uintptr_t a_rbx_offset)
			{
				test(rbx, rbx);
				jz("returnFunc");
				mov(rcx, dword[rbx + a_rbx_offset]);
				test(rcx, rcx);
				jz("returnFunc");
				test(rax, rax);
				jz("returnFunc");
				JumpTo(a_dest);

				L("returnFunc");
				JumpTo(a_rtn);
			}
		};
	}

	ModulePipBoyLightInv::ModulePipBoyLightInv() :
		Module("PipBoy Light Env", &bFixesPipBoyLightInv)
	{}

	bool ModulePipBoyLightInv::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto base = RE::ID::PlayerCharacter::UpdateActor3DPosition.address();

		REL::Relocation target{ base + REL::Offset { 0xCB2, 0xC92 }.offset() };
		REL::Relocation resume{ target.address() + 0x7 };
		REL::Relocation returnAddr{ base + REL::Offset { 0xDA7, 0xD87 }.offset() };

		const auto instructionBytes = resume.address() - target.address();
		for (std::size_t i = 0; i < instructionBytes; i++)
		{
			REL::WriteSafeData(target.address() + i, REL::NOP);
		}

		pipBoyDetail::Patch p{ resume.address(), returnAddr.address(), 0xC40 };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(target.address(), trampoline.allocate(p));

		return true;
	}
}