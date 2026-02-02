#include <Modules\AdModuleBGSAIWorldLocationRefRadius.h>
#include <AdUtils.h>
#include <AdAssert.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesBGSAIWorldLocationRefRadius{ "Fixes", "bBGSAIWorldLocationRefRadius", true };

	struct Patch : Xbyak::CodeGenerator
	{
		explicit Patch(std::uintptr_t a_dest, std::uintptr_t a_rtn)
		{
			Xbyak::Label contLab;
			Xbyak::Label retLab;

			// code clobbered at target is placed here
			movss(qword[rbx + 0x10], RELEX::IsRuntimeOG() ? xmm7 : xmm0);
			// end clobbered code
			test(rsi, rsi);    // nullptr check on rsi
			jz("returnFunc");  // jump to returnFunc if rsi is null
			jmp(ptr[rip + contLab]);

			L("returnFunc");
			jmp(ptr[rip + retLab]);

			L(contLab);
			dq(a_dest);

			L(retLab);
			dq(a_rtn);
		}
	};

	ModuleBGSAIWorldLocationRefRadius::ModuleBGSAIWorldLocationRefRadius() :
		Module("Module BGSAIWorldLocationRefRadius", &bPathesBGSAIWorldLocationRefRadius)
	{}

	bool ModuleBGSAIWorldLocationRefRadius::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleBGSAIWorldLocationRefRadius::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Base;
		REL::Relocation<std::uintptr_t> Target;
		REL::Relocation<std::uintptr_t> ReturnAddress;

		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			Base = REL::Relocation<std::uintptr_t>{ REL::ID(2188379) };
			Target = REL::Relocation<std::uintptr_t>{ Base.address() + 0x4E };
			ReturnAddress = REL::Relocation<std::uintptr_t>{ Base.address() + 0xF8 };
		}
		else
		{
			// OG
			Base = REL::Relocation<std::uintptr_t>{ REL::ID(964254) };
			Target = REL::Relocation<std::uintptr_t>{ Base.address() + 0x52 };
			ReturnAddress = REL::Relocation<std::uintptr_t>{ Base.address() + 0x104 };
		}

		REL::Relocation<std::uintptr_t> Resume = REL::Relocation<std::uintptr_t>{ Target.address() + 0x5 };

		const auto InstructionBytes = Resume.address() - Target.address();
		for (std::size_t i = 0; i < InstructionBytes; i++)
		{
			REL::WriteSafeData(Target.address() + i, REL::NOP);
		}

		Patch p{ Resume.address(), ReturnAddress.address() };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(Target.address(), trampoline.allocate(p));

		return true;
	}

	bool ModuleBGSAIWorldLocationRefRadius::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}