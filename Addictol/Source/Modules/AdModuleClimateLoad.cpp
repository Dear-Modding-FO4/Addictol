// #original: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/climate_load.h

#include <Modules/AdModuleClimateLoad.h>
#include <AdUtils.h>

#include <RE/B/BGSSaveLoadBuffer.h>
#include <RE/S/Sky.h>

namespace Addictol
{
	static REX::TOML::Bool<> bClimateLoad{ "Fixes"sv, "bClimateLoad"sv, true };

	struct Sky
	{
		// For some reason, the deletion array contains pointers not to REFR, but to some file.
		static void LoadGame(RE::Sky* a_this, RE::BGSSaveLoadBuffer* a_buffer) noexcept
		{
			LoadGame_orig(a_this, a_buffer);

			using Flags = RE::Sky::Flags;
			a_this->flags.set(Flags::kUpdateSunriseBegin, Flags::kUpdateSunriseEnd, Flags::kUpdateSunsetBegin,
				Flags::kUpdateSunsetEnd, Flags::kUpdateColorsSunriseBegin, Flags::kUpdateColorsSunsetEnd);
		}

		inline static decltype(LoadGame)* LoadGame_orig{ nullptr };
	};

	ModuleClimateLoadFix::ModuleClimateLoadFix() :
		Module("Climate Load", &bClimateLoad)
	{}

	bool ModuleClimateLoadFix::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleClimateLoadFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		*((uintptr_t*)&Sky::LoadGame_orig) = RELEX::DetourJump(REL::ID{ 531797, 2208883 }.address(),
			reinterpret_cast<uintptr_t>(&Sky::LoadGame));

		return true;
	}

	bool ModuleClimateLoadFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleClimateLoadFix::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
