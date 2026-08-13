#include <AdPlugin.h>
#include <AdUtils.h>
#include <AdConfigValidation.h>
#include <AdProfilerCore.h>
#include <AdProfilerDLL.h>
#include <AdProfilerMemory.h>
#include <AdZlibBackend.h>
#include <AdProfilerModules.h>

#include <RE/B/BSCRC32.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>

#include <Windows.h>
#undef ERROR

#define AD_LOGPLUGINHASHES 0
//#define AD_DEBUGBREAK 1

extern void AdRegisterModules();
extern void AdRegisterPreloadModules();

namespace Addictol
{
	static REX::TOML::Bool<> bAdditionalIgnoreCompatibilityChecks{ "Additional"sv, "bIgnoreCompatibilityChecks"sv, false };

	[[nodiscard]] static const char* GetF4SEMessageName(std::uint32_t a_type) noexcept
	{
		switch (a_type)
		{
		case F4SE::MessagingInterface::kPostLoad:      return "PostLoad";
		case F4SE::MessagingInterface::kPostPostLoad:  return "PostPostLoad";
		case F4SE::MessagingInterface::kPreLoadGame:   return "PreLoadGame";
		case F4SE::MessagingInterface::kPostLoadGame:  return "PostLoadGame";
		case F4SE::MessagingInterface::kPreSaveGame:   return "PreSaveGame";
		case F4SE::MessagingInterface::kPostSaveGame:  return "PostSaveGame";
		case F4SE::MessagingInterface::kDeleteGame:    return "DeleteGame";
		case F4SE::MessagingInterface::kInputLoaded:   return "InputLoaded";
		case F4SE::MessagingInterface::kNewGame:       return "NewGame";
		case F4SE::MessagingInterface::kGameLoaded:    return "GameLoaded";
		case F4SE::MessagingInterface::kGameDataReady: return "GameDataReady";
		default:                                       return "Unknown";
		}
	}

	static std::vector<const RE::TESFile*> AnalyzeGameCollectionCriticalCompatibility() noexcept
	{
		// Incompatible Mods
		std::vector<const RE::TESFile*> incompatibleMods;
		const std::array<uint32_t, 6> incompatibleModHashes
		{
			260600794, 1335048061,					// Fixer
			2498600491, 2948692632, 631466042,		// Unbound Worldspace
			2684648774								// Optimized Room Bounds
		};

		// Get DataHandler
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) return incompatibleMods;

		// Is Compatible
		auto IsCompatible = [&](const RE::TESFile* a_file)
		{
			// Get the Filename in Lowercase
			std::string fileName = a_file->filename;
			_strlwr(fileName.data());

			// Remove the File Extension
			auto it = fileName.find_last_of('.');
			if (it != std::string::npos) fileName.erase(it, -1);

			// Generate CRC32 Hash
			const auto hash = RE::detail::GenerateCRC32({ reinterpret_cast<uint8_t*>(fileName.data()), fileName.length() });

#if AD_LOGPLUGINHASHES
			// Log
			REX::INFO("Plugin: {}, Hash: {} / 0x{:08X}", a_file->filename, hash, hash);
#endif

			return std::find(incompatibleModHashes.begin(), incompatibleModHashes.end(), hash) == incompatibleModHashes.end();
		};

		// Analyze Files Array
		auto AnalyzeFilesArray = [&](const RE::BSTArray<RE::TESFile*>& files)
		{
			for (const auto* file : files)
			{
				if (!IsCompatible(file))
				{
					REX::ERROR("Found an incompatible mod: {}"sv, file->filename);
					incompatibleMods.push_back(file);
				}
			}
		};

#if AD_LOGPLUGINHASHES
		REX::INFO("======== LOGGING PLUGIN HASHES ========");
#endif

		AnalyzeFilesArray(dataHandler->compiledFileCollection.files);
		AnalyzeFilesArray(dataHandler->compiledFileCollection.smallFiles);

#if AD_LOGPLUGINHASHES
		REX::INFO("======== END OF PLUGIN HASHES ========");
#endif

		return incompatibleMods;
	}

	static void F4SEMessageListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		{
			auto profiler = ProfilerCore::GetSingleton();
			if (profiler->IsActive())
				profiler->MarkPhase(std::string("F4SE_"sv) + GetF4SEMessageName(a_msg->type));
		}

		auto plugin = Plugin::GetSingleton();
		if (!plugin->IsInstall())
		{
			// Install other patches by message type
			auto& moduleManager = plugin->GetModules();
			moduleManager.QueryAllByMessage(a_msg);
			moduleManager.InstallAllByMessage(a_msg);

			if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
			{
				if (!bAdditionalIgnoreCompatibilityChecks.GetValue())
				{
					const auto incompatibleMods = AnalyzeGameCollectionCriticalCompatibility();
					if (!incompatibleMods.empty())
					{
						std::string incompatibilityMessage = "Incompatible mods are installed, please disable Addictol or remove the following incompatible mods:\n\n";

						// Add Incompatible Mods to the Message
						for (const auto* file : incompatibleMods)
						{
							if (file)
							{
								incompatibilityMessage += "  - ";
								incompatibilityMessage += file->filename;
								incompatibilityMessage += '\n';
							}
						}

						incompatibilityMessage += "\nCheck the mod page's description for more info, we cannot provide support if you choose to ignore this warning and you do so at your own risk.";

						// Log
						REX::CRITICAL("{}"sv, incompatibilityMessage);

						// Message Box on a separate thread in order to not stall the rest of the mod
						// This is in case the user decides to just Alt + Tab or something like that
						std::thread([message = std::move(incompatibilityMessage)]
						{
							while (1)
							{
								const auto result = MessageBoxA(nullptr, message.c_str(), "Addictol - Incompatible Mods", MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
								if (result == IDRETRY)
									continue;
								else if (result == IDABORT)
								{
									// For debugger
									__debugbreak();
									// For Wine
									abort();
									// AGAIN!!!
									TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
									// CTD
									*((int*)0) = 0;
								}

								break;
							}
						}).detach();
					}
				}

				moduleManager.LogSummary();
				REX::INFO(""sv _PluginName " Initialized!"sv);
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

	bool Plugin::Init(const F4SE::LoadInterface* a_f4se)
	{
		if (isInit)
			return true;

		static std::once_flag once;
		std::call_once(once, [&]() {
#if AD_DEBUGBREAK
			MessageBoxA(nullptr, "Debugbreak load stage", "DEBUG", 0);
#endif

			// Init
			F4SE::Init(a_f4se);

			if (!isPreloadInit)
			{
				auto game_ver = a_f4se->RuntimeVersion();
				REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing..."sv);
				REX::INFO("Game version: {}.{}.{}.{}"sv, game_ver.major(), game_ver.minor(), game_ver.patch(), game_ver.build());

				// Get the Trampoline and Allocate
				auto& trampoline = REL::GetTrampoline();
				trampoline.create(AD_TRAMPOLINE_SIZE);

				// Load the Config
				const auto config = REX::TOML::SettingStore::GetSingleton();
				config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
				config->Load();
				InitializeZlibBackendConfig();

				// Validate config keys
				ValidateConfigKeys("Data/F4SE/Plugins/" _PluginName ".toml");
				ValidateConfigKeys("Data/F4SE/Plugins/" _PluginName "Custom.toml");

				// Early profiler start: install DLL profiler before other modules load
				if (ProfilerCore::IsEnabledInConfig())
				{
					auto profiler = ProfilerCore::GetSingleton();
					if (!profiler->IsActive())
						profiler->Start();
					profiler->MarkPhase("ConfigLoaded"sv);
					if (ProfilerCore::IsMemoryTrackingEnabled())
						ProfilerMemory::GetSingleton()->CaptureBaseline();
				}
			}

			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig() && ProfilerCore::IsDLLEnabled())
				ProfilerDLL::GetSingleton()->Install(a_f4se);

			// Register all modules
			AdRegisterModules();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
				ProfilerCore::GetSingleton()->MarkPhase("ModulesRegistered"sv);

			// Listen for Messages (to Install PostInit Patches)
			auto MessagingInterface = F4SE::GetMessagingInterface();
			if (MessagingInterface->RegisterListener(F4SEMessageListener))
				REX::INFO("Started Listening for F4SE Message Callbacks."sv);

			// Listen for Papyrus
			auto PapyrusInterface = F4SE::GetPapyrusInterface();
			if (PapyrusInterface->Register([](RE::BSScript::IVirtualMachine* vm) {
				F4SEPapyrusListener(vm);
				return true; }))
				REX::INFO("Started Listening for Papyrus Callbacks."sv);

			// Query patches
			moduleManager.QueryLoadAll();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
				ProfilerCore::GetSingleton()->MarkPhase("ModulesQueried"sv);
			// Install load patches
			moduleManager.InstallLoadAll();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
			{
				ProfilerCore::GetSingleton()->MarkPhase("ModulesInstalled"sv);
				ProfilerFlushModuleEntries();
				if (ProfilerCore::IsMemoryTrackingEnabled())
					ProfilerMemory::GetSingleton()->CaptureSnapshot("AfterModuleInstall"sv);
			}

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
			MessageBoxA(nullptr, "Debugbreak preload stage", "DEBUG", 0);
#endif
			// Preload Init
			F4SE::Init(a_preloadf4se);

			auto game_ver = a_preloadf4se->RuntimeVersion();
			REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing..."sv);
			REX::INFO("Game version: {}.{}.{}.{}"sv, game_ver.major(), game_ver.minor(), game_ver.patch(), game_ver.build());

			// Get the Trampoline and Allocate
			auto& trampoline = REL::GetTrampoline();
			trampoline.create(AD_TRAMPOLINE_SIZE);

			// Load the Config
			const auto config = REX::TOML::SettingStore::GetSingleton();
			config->Init("Data/F4SE/Plugins/" _PluginName ".toml", "Data/F4SE/Plugins/" _PluginName "Custom.toml");
			config->Load();
			InitializeZlibBackendConfig();

			// Validate config keys
			ValidateConfigKeys("Data/F4SE/Plugins/" _PluginName ".toml");
			ValidateConfigKeys("Data/F4SE/Plugins/" _PluginName "Custom.toml");

			// Early profiler start: install DLL profiler before other modules load
			if (ProfilerCore::IsEnabledInConfig())
			{
				auto profiler = ProfilerCore::GetSingleton();
				if (!profiler->IsActive())
					profiler->Start();
				profiler->MarkPhase("PreloadConfigLoaded"sv);
				if (ProfilerCore::IsMemoryTrackingEnabled())
					ProfilerMemory::GetSingleton()->CaptureBaseline();
			}

			// Register preload all modules
			AdRegisterPreloadModules();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
				ProfilerCore::GetSingleton()->MarkPhase("PreloadModulesRegistered"sv);
			// Query preload patches
			moduleManager.QueryPreloadAll();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
				ProfilerCore::GetSingleton()->MarkPhase("PreloadModulesQueried"sv);
			// Install  patches
			moduleManager.InstallPreloadAll();
			// Profiler phase
			if (ProfilerCore::IsEnabledInConfig())
			{
				ProfilerCore::GetSingleton()->MarkPhase("PreloadModulesInstalled"sv);
				ProfilerFlushModuleEntries();
			}

			isPreloadInit = true;
		});

		return isPreloadInit;
	}
}
