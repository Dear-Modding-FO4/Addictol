#include <Modules/AdModuleSafeExit.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Core/AdUtils.h>

#include <RE/S/Setting.h>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

namespace Addictol
{
	inline static void Shutdown() noexcept
	{
#if AD_TRACER
		MemoryTracer::GetSingleton()->Dump();
#endif
		REX::INFO("Shutting Down Game..."sv);
		// The skipped Main::Shutdown also saved Fallout4Prefs.ini.
		if (auto prefs = RE::INIPrefSettingCollection::GetSingleton(); prefs && prefs->Open(true))
		{
			prefs->WriteSettings();
			prefs->Close();
		}
		Telemetry::Hub().Stop();
		REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), EXIT_SUCCESS);
	}

	ModuleSafeExit::ModuleSafeExit() :
		Module("Safe Exit", &bFixesSafeExit)
	{}

	bool ModuleSafeExit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(REL::Relocation{ RE::ID::WinMain, REL::Offset{ 0x20, 0x20B, 0x20 } }.get(), Shutdown);

		return true;
	}
}
