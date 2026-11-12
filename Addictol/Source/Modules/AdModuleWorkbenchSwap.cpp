#include <Modules/AdModuleWorkbenchSwap.h>
#include <Core/AdUtils.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	namespace workbenchSwapDetail
	{
		struct Patch : RELEX::Stub
		{
			explicit Patch(std::uintptr_t a_dest)
			{
				lock();
				and_(dword[rdi + 0x4], 0xFFFFFFF);
				mov(rcx, 1);
				JumpTo(a_dest);
			}
		};
	}

	ModuleWorkbenchSwap::ModuleWorkbenchSwap() :
		Module("Workbench Swap", &bFixesWorkbenchSwap)
	{}

	bool ModuleWorkbenchSwap::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation Target{ RE::ID::BSSpinLock::BSReadWriteLock::lock_read, 0x48 };
		REL::Relocation Resume{ RE::ID::BSSpinLock::BSReadWriteLock::lock_read, 0x4D };

		workbenchSwapDetail::Patch p{ Resume.address() };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(Target.address(), trampoline.allocate(p));

		return true;
	}
}