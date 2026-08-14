#include <Modules/AdModuleSafeExit.h>
#include <AdProfilerBA2.h>
#include <AdUtils.h>

#include <RE/M/Main.h>

#include <thread>
#include <chrono>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

namespace Addictol
{
	static REX::TOML::Bool<> bFixesSafeExit{ "Fixes"sv, "bSafeExit"sv, true };
	static REX::TOML::I32<>  nAdditionalQuitGameDelayMs{ "Additional"sv, "nQuitGameDelayMs"sv, 2000 };

	inline static void Shutdown() noexcept
	{
		ProfilerBA2::GetSingleton()->PublishFinal("Shutdown"sv);
#if AD_TRACER
		MemoryTracer::GetSingleton()->Dump();
#endif
		REX::INFO("Shutting Down Game..."sv);
		REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), EXIT_SUCCESS);
	}

	// Based on BakaQuitGameFix by shad0wshayd3 (GPL-3.0 w/ modding exception).
	// Replaces Script::QuitGame so the main-thread cleanup call is skipped and
	// the quitGame flag is set from a detached thread after a short delay,
	// letting UI / menu callbacks unwind before the main loop exits.
	inline static bool QuitGame() noexcept
	{
		std::thread([]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(nAdditionalQuitGameDelayMs.GetValue()));
			if (auto main = RE::Main::GetSingleton())
				main->quitGame = true;
		}).detach();

		return true;
	}

	ModuleSafeExit::ModuleSafeExit() :
		Module("Safe Exit", &bFixesSafeExit)
	{}

	bool ModuleSafeExit::IsEnabledInConfig() noexcept
	{
		return bFixesSafeExit.GetValue();
	}

	bool ModuleSafeExit::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleSafeExit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(REL::Relocation{ REL::ID{ 668528, 2718225, 4812562 }, REL::Offset{ 0x20, 0x20B, 0x20 } }.get(), Shutdown);

		// Script::QuitGame — 0x29-byte function, identical shape across runtimes.
		// NG ID is reused on AE, so the trailing slot inherits 2205365 automatically.
		auto quitTarget = REL::Relocation{ REL::ID{ 1497968, 2205365 } };
		REL::WriteSafeFill(quitTarget.address(), REL::INT3, 0x29);
		RELEX::DetourJump(quitTarget.address(), reinterpret_cast<uintptr_t>(QuitGame));

		return true;
	}

	bool ModuleSafeExit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleSafeExit::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}