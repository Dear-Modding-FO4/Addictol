#include <Modules\AdModuleSafeExit.h>
#include <AdUtils.h>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

namespace Addictol
{
	static REX::TOML::Bool<> bFixesSafeExit{ "Fixes", "bSafeExit", true };

	inline static void Shutdown() noexcept
	{
#if AD_TRACER
		MemoryTracer::GetSingleton()->Dump();
#endif
		REX::INFO("Shutting Down Game...");
		REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), EXIT_SUCCESS);
	}

	ModuleSafeExit::ModuleSafeExit() :
		Module("Module Safe Exit", &bFixesSafeExit)
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
		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(REL::Relocation{ REL::ID{ 668528, 2718225, 4812562 }, REL::Offset{ 0x20, 0x20B, 0x20 } }.get(), Shutdown);

		return true;
	}

	bool ModuleSafeExit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}