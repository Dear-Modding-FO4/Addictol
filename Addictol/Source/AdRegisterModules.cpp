#include <AdPlugin.h>
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

void AdRegisterModules()
{
	auto plugin = Addictol::Plugin::GetSingleton();
	if (!plugin) return;

	auto& modules = plugin->GetModules();

	// Create patches
	static auto sModuleThreads						= std::make_shared<Addictol::ModuleThreads>();
	static auto sModuleGreyMovie					= std::make_shared<Addictol::ModuleGreyMovie>();
	static auto sModulePackageAllocateLocation		= std::make_shared<Addictol::ModulePackageAllocateLocation>();
	static auto sModuleLibDeflate					= std::make_shared<Addictol::ModuleLibDeflate>();
	static auto sModuleProfile						= std::make_shared<Addictol::ModuleProfile>();
	static auto sModuleLoadScreen					= std::make_shared<Addictol::ModuleLoadScreen>();
	static auto sModuleAchievements					= std::make_shared<Addictol::ModuleAchievements>();
	static auto sModuleLODDistance					= std::make_shared<Addictol::ModuleLODDistance>();
	static auto sModuleInitTints					= std::make_shared<Addictol::ModuleInitTints>();
	static auto sModuleActorIsHostileToActor		= std::make_shared<Addictol::ModuleActorIsHostileToActor>();
	static auto sModuleFacegen						= std::make_shared<Addictol::ModuleFacegen>();
	static auto sModuleBGSAIWorldLocationRefRadius	= std::make_shared<Addictol::ModuleBGSAIWorldLocationRefRadius>();
	static auto sModuleSafeExit						= std::make_shared<Addictol::ModuleSafeExit>();
	static auto sModuleUnalignedLoad				= std::make_shared<Addictol::ModuleUnalignedLoad>();
	static auto sModuleCellInit						= std::make_shared<Addictol::ModuleCellInit>();
	static auto sModuleMemoryManager				= std::make_shared<Addictol::ModuleMemoryManager>();
	static auto sModuleSmallblockAllocator			= std::make_shared<Addictol::ModuleSmallblockAllocator>();
	static auto sModuleScaleformAllocator			= std::make_shared<Addictol::ModuleScaleformAllocator>();
	static auto sModuleWorkbenchSwap				= std::make_shared<Addictol::ModuleWorkbenchSwap>();
	static auto sModuleDropItems					= std::make_shared<Addictol::ModuleDropItems>();

	// Registers preload patches
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

	// Registers other patches
	modules.Register(sModuleThreads,		Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleFacegen,		Addictol::ModuleManager::Type::kGameDataReady);
	modules.Register(sModuleSafeExit,		Addictol::ModuleManager::Type::kGameDataReady);
}