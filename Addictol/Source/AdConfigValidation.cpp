#include <AdConfigValidation.h>
#include <REX/REX.h>
#include <toml11/single_include/toml.hpp>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace Addictol
{
	using namespace std::literals;

	// Known config keys by section, derived from REX::TOML declarations across all modules.
	// Update this when adding or removing a config key.
	static const std::unordered_map<std::string_view, std::unordered_set<std::string_view>> s_knownKeys = {
		{ "Patches", {
			"bThreads", "bLibDeflate", "bLoadScreen", "bProfile", "bAchievements",
			"bFacegen", "bMemoryManager", "bSmallBlockAllocator", "bScaleformAllocator",
			"bBSMTAManager", "bBSPreCulledObjects", "bINISettingCollection",
			"bArchiveLimits", "bInputSwitch", "bFasterWorkshop",
			"bSaveAddedSoundCategories", "bCOMInit", "bHighResBloom", "bDpiScaling"
		}},
		{ "Fixes", {
			"bGreyMovie", "bPackageAllocateLocation", "bInitTints", "bLODDistance",
			"bActorIsHostileToActor", "bBGSAIWorldLocationRefRadius", "bSafeExit",
			"bUnalignedLoad", "bCellInit", "bWorkbenchSwap", "nMaxStdIO",
			"bMovementPlanner", "bEscapeFreeze", "bIOCacher",
			"bTESObjectREFRGetEncounterZone", "bPipBoyLightInv", "bInteriorNavCut",
			"bMagicEffectApplyEvent", "bEncounterZoneReset", "bLeveledListCrash",
			"bBakaMaxPapyrusOps", "bPapyrusGCBug", "bCreateD3DAndSwapchain",
			"bCheckInternetAccess", "bStolenPowerArmorOwnership", "bManyItems",
			"bCombatMusic", "bWorkbenchSound", "bActorCauseSaveBloat",
			"bAnimSignedCrash", "bBethesdaNetCrash",
			"bMuzzleFlashLight", "bAltTabFullscreen", "bPowerGridScrap",
			"bViewmodelShading", "bDofFix", "bUtilityShader",
			"bPipBoyCursorConstraints", "bToggleGrassCommand", "bTextureLoadCrash",
			"bDownwardAiming"
		}},
		{ "Warnings", {
			"bImageSpaceAdapter", "bDuplicateAddonNodeIndex", "bReferenceHandleLimit"
		}},
		{ "Additional", {
			"bDbgFacegenOutput", "bUseNewRedistributable",
			"uScaleformPageSize", "uScaleformHeapSize",
			"nSleepTimer", "nMaxLockCount",
			"bInteriorNavCutMultiThreading", "nMaxPapyrusOpsPerFrame",
			"bIgnorePreInstallBias", "nQuitGameDelayMs", "nBloomScale"
		}},
		{ "Profiler", {
			"bProfiler", "bESPProfiler", "bESPSubHooks", "bDLLProfiler",
			"bModuleProfiler", "bStartupTimeline", "bMemoryTracking",
			"bBA2Timing", "bCSVExport"
		}}
	};

	void ValidateConfigKeys(const char* a_filePath) noexcept
	{
		auto result = toml::try_parse(a_filePath);
		if (!result.is_ok())
			return;

		auto& data = result.unwrap();
		if (!data.is_table())
			return;

		for (auto& [sectionName, sectionValue] : data.as_table())
		{
			auto sectionIt = s_knownKeys.find(sectionName);
			if (sectionIt == s_knownKeys.end())
			{
				REX::WARN("Config: unknown section [{}] in \"{}\""sv, sectionName, a_filePath);
				continue;
			}

			if (!sectionValue.is_table())
				continue;

			for (auto& [keyName, keyValue] : sectionValue.as_table())
			{
				if (!sectionIt->second.contains(keyName))
					REX::WARN("Config: unknown key \"{}\" in [{}] in \"{}\""sv, keyName, sectionName, a_filePath);
			}
		}
	}
}
