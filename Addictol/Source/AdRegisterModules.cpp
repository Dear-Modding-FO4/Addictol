#include <AdPlugin.h>
#include <AdUtils.h>
#include <Modules\AdModuleThreads.h>
#include <Modules\AdModuleGreyMovie.h>
#include <Modules\AdModulePackageAllocateLocation.h>
#include <Modules\AdModuleLibDeflate.h>
#include <Modules\AdModuleProfile.h>
#include <Modules\AdModuleLoadScreen.h>
#include <Modules\AdModuleAchievements.h>
#include <Modules\AdModuleLODDistance.h>
#include <Modules\AdModuleInitTints.h>
#include <Modules\AdModuleActorIsHostileToActor.h>
#include <Modules\AdModuleFacegen.h>
#include <Modules\AdModuleBGSAIWorldLocationRefRadius.h>
#include <Modules\AdModuleSafeExit.h>
#include <Modules\AdModuleUnalignedLoad.h>
#include <Modules\AdModuleCellInit.h>
#include <Modules\AdModuleMemoryManager.h>
#include <Modules\AdModuleSmallblockAllocator.h>
#include <Modules\AdModuleScaleformAllocator.h>
#include <Modules\AdModuleWorkbenchSwap.h>
#include <Modules\AdModuleDropItems.h>
#include <Modules\AdModuleMaxStdIO.h>
#include <Modules\AdModuleMovementPlanner.h>
#include <Modules\AdModuleEscapeFreeze.h>
#include <Modules\AdModuleIOCacher.h>
#include <Modules\AdModuleBSPreCulledObjects.h>
#include <Modules\AdModuleTESObjectREFRGetEncounterZone.h>
#include <Modules\AdModuleINISettingCollection.h>
#include <Modules\AdModulePipBoyLightInv.h>
#include <Modules\AdModuleInteriorNavCut.h>
#include <Modules\AdModuleControlSamplers.h>
#include <Modules\AdModuleMagicEffectApplyEvent.h>
#include <Modules\AdModuleEncounterZoneReset.h>
#include <Modules\AdModuleArchiveLimits.h>
#include <Modules\AdModuleImageSpaceAdapterWarning.h>
#include <Modules\AdModuleDuplicateAddonNodeIndex.h>
#include <Modules\AdModuleLeveledListEntryCount.h>

// Create patches
static auto sModuleThreads							= std::make_shared<Addictol::ModuleThreads>();
static auto sModuleGreyMovie						= std::make_shared<Addictol::ModuleGreyMovie>();
static auto sModulePackageAllocateLocation			= std::make_shared<Addictol::ModulePackageAllocateLocation>();
static auto sModuleLibDeflate						= std::make_shared<Addictol::ModuleLibDeflate>();
static auto sModuleProfile							= std::make_shared<Addictol::ModuleProfile>();
static auto sModuleLoadScreen						= std::make_shared<Addictol::ModuleLoadScreen>();
static auto sModuleAchievements						= std::make_shared<Addictol::ModuleAchievements>();
static auto sModuleLODDistance						= std::make_shared<Addictol::ModuleLODDistance>();
static auto sModuleInitTints						= std::make_shared<Addictol::ModuleInitTints>();
static auto sModuleActorIsHostileToActor			= std::make_shared<Addictol::ModuleActorIsHostileToActor>();
static auto sModuleFacegen							= std::make_shared<Addictol::ModuleFacegen>();
static auto sModuleBGSAIWorldLocationRefRadius		= std::make_shared<Addictol::ModuleBGSAIWorldLocationRefRadius>();
static auto sModuleSafeExit							= std::make_shared<Addictol::ModuleSafeExit>();
static auto sModuleUnalignedLoad					= std::make_shared<Addictol::ModuleUnalignedLoad>();
static auto sModuleCellInit							= std::make_shared<Addictol::ModuleCellInit>();
static auto sModuleMemoryManager					= std::make_shared<Addictol::ModuleMemoryManager>();
static auto sModuleSmallblockAllocator				= std::make_shared<Addictol::ModuleSmallblockAllocator>();
static auto sModuleScaleformAllocator				= std::make_shared<Addictol::ModuleScaleformAllocator>();
static auto sModuleWorkbenchSwap					= std::make_shared<Addictol::ModuleWorkbenchSwap>();
static auto sModuleDropItems						= std::make_shared<Addictol::ModuleDropItems>();
static auto sModuleMaxStdIO							= std::make_shared<Addictol::ModuleMaxStdIO>();
static auto sModuleMovementPlanner					= std::make_shared<Addictol::ModuleMovementPlanner>();
static auto sModuleEscapeFreeze						= std::make_shared<Addictol::ModuleEscapeFreeze>();
static auto sModuleIOCacher							= std::make_shared<Addictol::ModuleIOCacher>();
static auto sModuleBSPreCulledObjects				= std::make_shared<Addictol::ModuleBSPreCulledObjects>();
static auto sModuleTESObjectREFRGetEncounterZone	= std::make_shared<Addictol::ModuleTESObjectREFRGetEncounterZone>();
static auto sModuleINISettingCollection				= std::make_shared<Addictol::ModuleINISettingCollection>();
static auto sModulePipBoyLightInv					= std::make_shared<Addictol::ModulePipBoyLightInv>();
static auto sModuleInteriorNavCut					= std::make_shared<Addictol::ModuleInteriorNavCut>();
static auto sModuleControlSamplers					= std::make_shared<Addictol::ModuleControlSamplers>();
static auto sModuleMagicEffectApplyEvent			= std::make_shared<Addictol::ModuleMagicEffectApplyEvent>();
static auto sModuleEncounterZoneReset				= std::make_shared<Addictol::ModuleEncounterZoneReset>();
static auto sModuleArchiveLimits					= std::make_shared<Addictol::ModuleArchiveLimits>();
static auto sModuleImageSpaceAdapterWarning			= std::make_shared<Addictol::ModuleImageSpaceAdapterWarning>();
static auto sModuleDuplicateAddonNodeIndex			= std::make_shared<Addictol::ModuleDuplicateAddonNodeIndex>();
static auto sModuleLeveledListEntryCount			= std::make_shared<Addictol::ModuleLeveledListEntryCount>();

void AdRegisterPreloadModules()
{
	auto plugin = Addictol::Plugin::GetSingleton();
	if (!plugin) return;

	auto& modules = plugin->GetModules();

	// Registers preload stage patches
	modules.Register(sModuleMaxStdIO);
}

void AdRegisterModules()
{
	// for OG preload stage not implemented
	if (RELEX::IsRuntimeOG())
		AdRegisterPreloadModules();

	auto plugin = Addictol::Plugin::GetSingleton();
	if (!plugin) return;

	auto& modules = plugin->GetModules();

	// Registers load stage patches
	modules.Register(sModuleGreyMovie);
	modules.Register(sModulePackageAllocateLocation);
	modules.Register(sModuleLibDeflate);
	modules.Register(sModuleProfile);
	modules.Register(sModuleLoadScreen);
	modules.Register(sModuleAchievements);
	modules.Register(sModuleLODDistance);
	modules.Register(sModuleInitTints);
	modules.Register(sModuleActorIsHostileToActor);
	modules.Register(sModuleFacegen);
	modules.Register(sModuleBGSAIWorldLocationRefRadius);
	modules.Register(sModuleSafeExit);
	modules.Register(sModuleUnalignedLoad);
	modules.Register(sModuleCellInit);
	modules.Register(sModuleMemoryManager);
	modules.Register(sModuleSmallblockAllocator);
	modules.Register(sModuleScaleformAllocator);
	modules.Register(sModuleWorkbenchSwap);
	modules.Register(sModuleDropItems);
	modules.Register(sModuleMovementPlanner);
	modules.Register(sModuleEscapeFreeze);
	modules.Register(sModuleIOCacher);
	modules.Register(sModuleBSPreCulledObjects);
	modules.Register(sModuleTESObjectREFRGetEncounterZone);
	modules.Register(sModuleINISettingCollection);
	modules.Register(sModulePipBoyLightInv);
	modules.Register(sModuleInteriorNavCut);
	modules.Register(sModuleMagicEffectApplyEvent);
	modules.Register(sModuleArchiveLimits);
	modules.Register(sModuleImageSpaceAdapterWarning);
	
	// Registers other patches
	modules.Register(sModuleThreads,					Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleFacegen,					Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleSafeExit,					Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleInteriorNavCut,				Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleControlSamplers,			Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleDuplicateAddonNodeIndex,	Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleLeveledListEntryCount,		Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleEncounterZoneReset,			Addictol::ModuleManager::Type::kGameLoaded);
}