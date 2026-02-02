#include <Modules\AdModuleSafeExit.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesSafeExit{ "Patches", "bSafeExit", true };

	inline void Shutdown()
	{
		REX::INFO("Shutting Down Game...");
		REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), EXIT_SUCCESS);
	}

	ModuleSafeExit::ModuleSafeExit() :
		Module("Module Safe Exit", &bPathesSafeExit)
	{}

	bool ModuleSafeExit::DoQuery() const noexcept
	{
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