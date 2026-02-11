#include <AdPlugin.h>
#include <AdUtils.h>

#include <RE\B\BSScriptUtil.h>

//#define AD_DEBUGBREAK 1
#if AD_DEBUGBREAK
#	include <windows.h>
#endif

extern void AdRegisterModules();
extern void AdRegisterPreloadModules();

namespace Addictol
{
#if SUPPORT_OG
	// For support OG
	static F4SE::Impl::F4SEInterface RestoreLoadInterface;
#endif // SUPPORT_OG

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
		else
			// Listener after installed for modules
			plugin->GetModules().ListenerLoadAllByMessage(a_msg);
	}

	static void F4SEPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		Plugin::GetSingleton()->GetModules().ListenerAllPapyrus(a_vm);
	}

#if SUPPORT_OG
	// Added F4SE 0.7.1+
	[[nodiscard]] inline static const char* F4SEAPI F4SEGetSaveFolderName() noexcept
	{
		return Addictol::GetSaveFolderName();
	}
#endif // SUPPORT_OG

	bool Plugin::Init(const F4SE::LoadInterface* a_f4se)
	{
		if (isInit)
			return true;

		static std::once_flag once;
		std::call_once(once, [&]() {
#if AD_DEBUGBREAK
			MessageBoxA(0, "Debugbreak load stage", "DEBUG", 0);
#endif
#if SUPPORT_OG
			memcpy(&RestoreLoadInterface, a_f4se, 48 /* OG struct size */);
			(((F4SE::Impl::F4SEInterface*)(&RestoreLoadInterface))->GetSaveFolderName) = F4SEGetSaveFolderName;
			// Init
			F4SE::Init((const F4SE::LoadInterface*)(&RestoreLoadInterface));
#else
			// Init
			F4SE::Init(a_f4se);
#endif // SUPPORT_OG

			if (!isPreloadInit)
			{
				auto game_ver = a_f4se->RuntimeVersion();
				REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing...");
				REX::INFO("Game version: {}.{}.{}.{}", game_ver.major(), game_ver.minor(), game_ver.patch(), game_ver.build());

				// Get the Trampoline and Allocate
				auto& trampoline = REL::GetTrampoline();
				trampoline.create(AD_TRAMPOLINE_SIZE);

				// Load the Config
				const auto config = REX::TOML::SettingStore::GetSingleton();
				config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
				config->Load();
			}

			// Register all modules
			AdRegisterModules();

			// Listen for Messages (to Install PostInit Patches)
			auto MessagingInterface = F4SE::GetMessagingInterface();
			if (MessagingInterface->RegisterListener(F4SEMessageListener))
				REX::INFO("Started Listening for F4SE Message Callbacks.");

			// Listen for Papyrus
			auto PapyrusInterface = F4SE::GetPapyrusInterface();
			if (PapyrusInterface->Register([](RE::BSScript::IVirtualMachine* vm) -> bool {
				F4SEPapyrusListener(vm);
				return true; }))
				REX::INFO("Started Listening for Papyrus Callbacks.");	

			// Install load patches
			moduleManager.QueryLoadAll();
			moduleManager.InstallLoadAll();

			isInit = true;
		});

		return isInit;
	}

	bool Plugin::PreloadInit(const F4SE::PreLoadInterface* a_preloadf4se)
	{
		if (isPreloadInit)
			return true;

		static std::once_flag once;
		std::call_once(once, [&]() {
#if AD_DEBUGBREAK
			MessageBoxA(0, "Debugbreak preload stage", "DEBUG", 0);
#endif
			// Preload Init
			F4SE::Init(a_preloadf4se);

			auto game_ver = a_preloadf4se->RuntimeVersion();
			REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing...");
			REX::INFO("Game version: {}.{}.{}.{}", game_ver.major(), game_ver.minor(), game_ver.patch(), game_ver.build());

			// Get the Trampoline and Allocate
			auto& trampoline = REL::GetTrampoline();
			trampoline.create(AD_TRAMPOLINE_SIZE);

			// Load the Config
			const auto config = REX::TOML::SettingStore::GetSingleton();
			config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
			config->Load();

			AdRegisterPreloadModules();

			// Install preload patches
			moduleManager.QueryPreloadAll();
			moduleManager.InstallPreloadAll();

			isPreloadInit = true;
		});

		return isPreloadInit;
	}
}