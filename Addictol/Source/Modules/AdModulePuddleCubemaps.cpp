#include <Modules/AdModulePuddleCubemaps.h>
#include <Core/AdUtils.h>

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESWorldSpace.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPuddleCubemaps{"Fixes"sv, "bPuddleCubemaps"sv, false};

	ModulePuddleCubemaps::ModulePuddleCubemaps() :
		Module("Puddle Cubemaps", &bFixesPuddleCubemaps)
	{}

	bool ModulePuddleCubemaps::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler)
			{
				REX::WARN("PuddleCubemaps: TESDataHandler was nullptr. Patch was not applied."sv);
				return true;
			}

			auto &worldspaces = dataHandler->GetFormArray<RE::TESWorldSpace>();
			if (worldspaces.empty())
			{
				REX::WARN("PuddleCubemaps: Worldspace array was empty. Patch was not applied."sv);
				return true;
			}

			RE::BSFixedString blankTextureName = RE::BSFixedString("");

			for (RE::TESWorldSpace *ws : worldspaces)
			{
				if (!ws)
					continue;

				ws->waterEnvMap.textureName = blankTextureName;
			}

			REX::INFO("PuddleCubemaps: Cleared water environment-map cubemaps on {} worldspace(s)."sv, worldspaces.size());
		}

		return true;
	}

}
