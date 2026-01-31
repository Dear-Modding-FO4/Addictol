#include <AdPlugin.h>
#include <AdIDs.h>
#include <REX\REX\Singleton.h>

extern void AdRegisterModules();

namespace Addictol
{
	static void F4SEMessageListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto plugin = Plugin::GetSingleton();
		if (!plugin->IsInstall())
		{
			// Install other patches by message type
			auto& moduleManager = plugin->GetModules();
			moduleManager.QueryAllByMessage(a_msg);
			moduleManager.InstallAllByMessage(a_msg);

			if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
			{
				REX::INFO("" _PluginName " Initialized!");
				plugin->SetAsInstall();
			}
		}
	}

	bool Plugin::Init(const F4SE::LoadInterface* a_f4se)
	{
		if (isInit)
			return true;

		// Init
		F4SE::Init(a_f4se);
		REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing...");

		// Update IDs for commonlibf4
		RE_MERGE::ID::UpdateIDs();

		// Register all modules
		AdRegisterModules();

		// Load the Config
		const auto config = REX::TOML::SettingStore::GetSingleton();
		config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
		config->Load();

		// Listen for Messages (to Install PostInit Patches)
		auto MessagingInterface = F4SE::GetMessagingInterface();
		MessagingInterface->RegisterListener(F4SEMessageListener);
		REX::INFO("Started Listening for F4SE Message Callbacks.");

		// Install preload patches
		moduleManager.QueryPreloadAll();
		moduleManager.InstallPreloadAll();

		isInit = true;
		return isInit;
	}
}