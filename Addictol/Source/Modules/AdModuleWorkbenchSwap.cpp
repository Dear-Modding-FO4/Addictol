#include <Modules\AdModuleWorkbenchSwap.h>
#include <AdUtils.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWorkbenchSwap{ "Fixes"sv, "bWorkbenchSwap"sv, true };

	struct Patch : Xbyak::CodeGenerator
	{
		explicit Patch(std::uintptr_t a_dest)
		{
			Xbyak::Label retLab;

			and_(dword[rdi + 0x4], 0xFFFFFFF);
			mov(rcx, 1);
			jmp(ptr[rip + retLab]);

			L(retLab);
			dq(a_dest);
		}
	};

	ModuleWorkbenchSwap::ModuleWorkbenchSwap() :
		Module("Workbench Swap", &bFixesWorkbenchSwap)
	{}

	bool ModuleWorkbenchSwap::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleWorkbenchSwap::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target{ REL::ID{ 1573164, 2267897 }, 0x48 };
		REL::Relocation<std::uintptr_t> Resume{ REL::ID{ 1573164, 2267897 }, 0x4D };

		Patch p{ Resume.address() };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(Target.address(), trampoline.allocate(p));

		return true;
	}

	bool ModuleWorkbenchSwap::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleWorkbenchSwap::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}