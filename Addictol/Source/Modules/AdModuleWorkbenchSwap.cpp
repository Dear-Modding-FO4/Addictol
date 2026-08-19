#include <Modules/AdModuleWorkbenchSwap.h>
#include <AdUtils.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWorkbenchSwap{ "Fixes"sv, "bWorkbenchSwap"sv, true };

	namespace workbenchSwapDetail
	{
		struct Patch : Xbyak::CodeGenerator
		{
			explicit Patch(std::uintptr_t a_dest)
			{
				Xbyak::Label retLab;

				lock();
				and_(dword[rdi + 0x4], 0xFFFFFFF);
				mov(rcx, 1);
				jmp(ptr[rip + retLab]);

				L(retLab);
				dq(a_dest);
			}
		};
	}

	ModuleWorkbenchSwap::ModuleWorkbenchSwap() :
		Module("Workbench Swap", &bFixesWorkbenchSwap)
	{}

	bool ModuleWorkbenchSwap::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation Target{ REL::ID{ 1573164, 2267897 }, 0x48 };
		REL::Relocation Resume{ REL::ID{ 1573164, 2267897 }, 0x4D };

		workbenchSwapDetail::Patch p{ Resume.address() };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(Target.address(), trampoline.allocate(p));

		return true;
	}

}