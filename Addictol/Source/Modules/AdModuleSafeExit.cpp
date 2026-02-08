#include <Modules\AdModuleSafeExit.h>
#include <AdUtils.h>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

namespace Addictol
{
	static REX::TOML::Bool<> bPathesSafeExit{ "Fixes", "bSafeExit", true };

	inline static void Shutdown() noexcept
	{
#if AD_TRACER
		MemoryTracer::GetSingleton()->Dump();
#endif
		REX::INFO("Shutting Down Game...");
		REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), EXIT_SUCCESS);
	}

	ModuleSafeExit::ModuleSafeExit() :
		Module("Module Safe Exit", &bPathesSafeExit)
	{}

	bool ModuleSafeExit::DoQuery() const noexcept
	{
		// This patch useless with BakaQuitGameFix.dll installed and no called
		//return !(RELEX::IsRuntimeAE() && REX::W32::GetModuleHandleA("BakaQuitGameFix.dll"));
		// Patch installed after
		return true;
	}

	bool ModuleSafeExit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target;
		std::size_t Offset;

		if (RELEX::IsRuntimeAE())
		{
			// AE
			Target = REL::ID(4812562);
			Offset = 0x20;
		}
		else if (RELEX::IsRuntimeNG())
		{
			// NG
			Target = REL::ID(2718225);
			Offset = 0x20B;
		}
		else
		{
			// OG
			Target = REL::ID(668528);
			Offset = 0x20;
		}

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(Target.address() + Offset, Shutdown);

		return true;
	}

	bool ModuleSafeExit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}