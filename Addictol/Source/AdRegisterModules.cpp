#include <AdPlugin.h>
#include <Modules\AdModuleThreads.h>

void AdRegisterModules()
{
	auto plugin = Addictol::Plugin::GetSingleton();
	if (!plugin) return;

	auto& modules = plugin->GetModules();

	// Registers preload patches
	modules.Register(new Addictol::ModuleThreads());

	// Registers other patches

	// TODO
}