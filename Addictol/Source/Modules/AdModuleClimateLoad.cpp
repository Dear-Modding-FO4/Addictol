// #original: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/climate_load.h

#include <Modules/AdModuleClimateLoad.h>
#include <Core/AdUtils.h>

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

	bool ModuleClimateLoadFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg)
			return false;

		const auto target = REL::ID{ 531797, 2208883 }.address();

		if (!RELEX::Validate(target, { 0x4C, 0x8B, 0xDC, 0x53, 0x55 }))
			return false;

		*((uintptr_t*)&Sky::LoadGame_orig) = RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&Sky::LoadGame));
		return Sky::LoadGame_orig != nullptr;
	}

}
