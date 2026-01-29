#include <AdPlugin.h>
#include <REX\REX\Singleton.h>

extern void AdRegisterModules();

namespace Addictol
{
	bool Plugin::Init(const F4SE::LoadInterface* a_f4se)
	{
		if (isInit)
			return true;

		// Init
		F4SE::Init(a_f4se);
		REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing...");

		// Register all modules
		AdRegisterModules();

		// Load the Config
		const auto config = REX::TOML::SettingStore::GetSingleton();
		config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
		config->Load();

		isInit = true;

		moduleManager.QueryAll();
		moduleManager.InstallAll();
	
		return isInit;
	}
}