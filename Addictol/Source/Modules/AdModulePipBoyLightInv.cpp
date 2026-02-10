#include <Modules\AdModulePipBoyLightInv.h>
#include <AdUtils.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesPipBoyLightInv{ "Fixes", "bPipBoyLightInv", true };

	struct Patch : Xbyak::CodeGenerator
	{
		explicit Patch(std::uintptr_t a_dest, std::uintptr_t a_rtn, std::uintptr_t a_rbx_offset)
		{
			Xbyak::Label contLab;
			Xbyak::Label retLab;

			test(rbx, rbx);
			jz("returnFunc");
			mov(rcx, dword[rbx + a_rbx_offset]);
			test(rcx, rcx);
			jz("returnFunc");
			test(rax, rax);
			jz("returnFunc");
			jmp(ptr[rip + contLab]);

			L("returnFunc");
			jmp(ptr[rip + retLab]);

			L(contLab);
			dq(a_dest);

			L(retLab);
			dq(a_rtn);
		}
	};

	ModulePipBoyLightInv::ModulePipBoyLightInv() :
		Module("Module PipBoy Light Env", &bPatchesPipBoyLightInv)
	{}

	bool ModulePipBoyLightInv::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePipBoyLightInv::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto base = REL::ID{ 566261, 2233255 }.address();

		REL::Relocation<std::uintptr_t> target{ base + REL::Offset { 0xCB2, 0xC92 }.offset() };
		REL::Relocation<std::uintptr_t> resume{ target.address() + 0x7 };
		REL::Relocation<std::uintptr_t> returnAddr{ base + REL::Offset { 0xDA7, 0xD87 }.offset() };

		const auto instructionBytes = resume.address() - target.address();
		for (std::size_t i = 0; i < instructionBytes; i++)
		{
			REL::WriteSafeData(target.address() + i, REL::NOP);
		}

		Patch p{ resume.address(), returnAddr.address(), 0xC40 };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(target.address(), trampoline.allocate(p));

		return true;
	}

	bool ModulePipBoyLightInv::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePipBoyLightInv::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}