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

void AdRegisterModules()
{
	auto plugin = Addictol::Plugin::GetSingleton();
	if (!plugin) return;

	auto& modules = plugin->GetModules();

	// Registers preload patches
	modules.Register(new Addictol::ModuleThreads());
	modules.Register(new Addictol::ModuleGreyMovie());
	modules.Register(new Addictol::ModulePackageAllocateLocation());
	modules.Register(new Addictol::ModuleLibDeflate());
	modules.Register(new Addictol::ModuleProfile());
	modules.Register(new Addictol::ModuleLoadScreen());
	modules.Register(new Addictol::ModuleAchievements());
	modules.Register(new Addictol::ModuleLODDistance());
	modules.Register(new Addictol::ModuleInitTints());

	// Registers other patches

	// TODO
}