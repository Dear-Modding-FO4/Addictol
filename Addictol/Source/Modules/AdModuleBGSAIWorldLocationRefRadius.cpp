#include <Modules/AdModuleBGSAIWorldLocationRefRadius.h>
#include <Core/AdUtils.h>

#include <xbyak/xbyak.h>

namespace Addictol
{

	namespace bgsAIWorldLocationRefRadiusDetail
	{
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
	}

	ModuleBGSAIWorldLocationRefRadius::ModuleBGSAIWorldLocationRefRadius() :
		Module("BGSAIWorldLocationRefRadius", &bFixesBGSAIWorldLocationRefRadius)
	{}

	bool ModuleBGSAIWorldLocationRefRadius::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto base = REL::Relocation{ REL::ID{ 964254, 2188379 } }.address();
		const auto target = base + REL::Offset{ 0x52, 0x4E }.offset();
		const auto returnAddr = base + REL::Offset{ 0x104, 0xF8 }.offset();
		const std::size_t size = 0x5;

		REL::WriteSafeFill(target, REL::NOP, size);
		RELEX::XbyakJump<bgsAIWorldLocationRefRadiusDetail::Patch>(target, target + size, returnAddr);

		return true;
	}

}