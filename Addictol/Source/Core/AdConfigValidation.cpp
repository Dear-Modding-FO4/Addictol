#include <Core/AdConfigValidation.h>
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
		{ "Patches"sv, {
			"bThreads"sv, "bLibDeflate"sv, "bLoadScreen"sv, "bProfile"sv, "bAchievements"sv,
			"bFacegen"sv, "bMemoryManager"sv, "bSmallBlockAllocator"sv, "bSmallBlockAllocatorUseSelectedHeap"sv, "bScaleformAllocator"sv,
			"bBSMTAManager"sv, "bBSPreCulledObjects"sv, "bINISettingCollection"sv,
			"bArchiveLimits"sv, "bInputSwitch"sv, "bFasterWorkshop"sv,
			"bSaveAddedSoundCategories"sv, "bCOMInit"sv, "bHighResBloom"sv, "bDpiScaling"sv,
			"bSaveCompression"sv, "bAudioSwitch"sv, "bHighResLocalMaps"sv
		}},
		{ "Fixes"sv, {
			"bGreyMovie"sv, "bPackageAllocateLocation"sv, "bInitTints"sv, "bLODDistance"sv,
			"bActorIsHostileToActor"sv, "bBGSAIWorldLocationRefRadius"sv, "bSafeExit"sv,
			"bUnalignedLoad"sv, "bCellInit"sv, "bWorkbenchSwap"sv, "nMaxStdIO"sv,
			"bMovementPlanner"sv, "bEscapeFreeze"sv, "bIOCacher"sv, "bCosaveIO"sv,
			"bTESObjectREFRGetEncounterZone"sv, "bPipBoyLightInv"sv, "bInteriorNavCut"sv,
			"bMagicEffectApplyEvent"sv, "bMagicEffectConditions"sv,
			"bEncounterZoneReset"sv, "bLeveledListCrash"sv,
			"bBakaMaxPapyrusOps"sv, "bPapyrusGCBug"sv, "bCreateD3DAndSwapchain"sv,
			"bCheckInternetAccess"sv, "bStolenPowerArmorOwnership"sv, "bManyItems"sv,
			"bCombatMusic"sv, "bWorkbenchSound"sv, "bActorCauseSaveBloat"sv,
			"bAnimSignedCrash"sv, "bBethesdaNetCrash"sv,
			"bMuzzleFlashLight"sv, "bAltTabFullscreen"sv, "bPowerGridScrap"sv,
			"bViewmodelShading"sv, "bDofFix"sv, "bUtilityShader"sv,
			"bPipBoyCursorConstraints"sv, "bToggleGrassCommand"sv, "bTextureLoadCrash"sv,
			"bFullPrecisionDecals"sv, "bMagicKeywordCrash"sv, "bAttachLightCrash"sv, "bDownwardAiming"sv,
			"bSprintStutter"sv, "bMoonRotation"sv, "bWeaponDebrisCrash"sv,
			"bCrashRemoveRefFix"sv, "bClimateLoad"sv, "bMusicOverlap"sv, "bPuddleCubemaps"sv,
			"bCompanionStrayBullet"sv, "bStringPoolRelease"sv, "bLoadOrder"sv, "bHUDMessageQueue"sv,
			"bWaterJetpackFix"sv, "bAIProcess3DUpdateFlag"sv, "bCraftingMenuFix"sv
		}},
		{ "Warnings"sv, {
			"bImageSpaceAdapter"sv, "bDuplicateAddonNodeIndex"sv, "bReferenceHandleLimit"sv
		}},
		{ "Others"sv, {
			"bRobCoPatcherCache"sv
		}},
		{ "Telemetry"sv, {
			"bEnabled"sv, "uSampleMs"sv, "uFrameRecordMs"sv, "bCsv"sv,
			"bPluginTiming"sv, "bFormLoadTiming"sv
		}},
		{ "Additional"sv, {
			"sAllocator"sv, "sZlibBackend"sv, "sLogLevel"sv, "sLogFlushLevel"sv,
			"bDbgFacegenOutput"sv, "bUseNewRedistributable"sv,
			"uScaleformPageSize"sv, "uScaleformHeapSize"sv,
			"nSleepTimer"sv, "nMaxLockCount"sv,
			"bInteriorNavCutMultiThreading"sv, "bFullPrecisionDecalsMembrane"sv,
			"bFullPrecisionDecalsEffectShaders"sv, "nMaxPapyrusOpsPerFrame"sv,
			"bIgnorePreInstallBias"sv, "nQuitGameDelayMs"sv, "nBloomScale"sv,
			"bRobCoPatcherCacheValidate"sv, "bIgnoreCompatibilityChecks"sv,
			"fLocalMapScaleFactor"sv,
			"bMenu"sv, "sMenuToggleKey"sv, "uMenuRefreshMs"sv
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
