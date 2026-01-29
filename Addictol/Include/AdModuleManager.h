#pragma once

#include <AdModule.h>
#include <map>
#include <string_view>

namespace Addictol
{
	class ModuleManager
	{
		std::map<std::string_view, Module*> modules;
		std::map<uint8_t, std::map<std::string_view, Module*>> rl_modules;

		ModuleManager(const ModuleManager&) = delete;
		ModuleManager operator=(const ModuleManager&) = delete;

		[[nodiscard]] bool SafeQueryMod(const Module* a_mod);
		[[nodiscard]] bool SafeInstallMod(Module* a_mod, F4SE::MessagingInterface::Message* a_msg = nullptr);
	public:
		enum class Type : uint8_t
		{
			kPreload = 0,
			kPostLoad,
			kPostPostLoad,
			kPreLoadGame,
			kPostLoadGame,
			kPreSaveGame,
			kPostSaveGame,
			kDeleteGame,
			kInputLoaded,
			kNewGame,
			kGameLoaded,
			kGameDataReady
		};

		ModuleManager() = default;
		virtual ~ModuleManager() = default;

		virtual bool Register(Module* a_mod, Type a_type = Type::kPreload) noexcept;
		virtual bool Unregister(const Module* a_mod, Type a_type = Type::kPreload) noexcept;
		virtual bool UnregisterByName(const char* a_name, Type a_type = Type::kPreload) noexcept;

		virtual void QueryPreloadAll() noexcept;
		virtual void InstallPreloadAll() noexcept;
		virtual void QueryAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
		virtual void InstallAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
	};
}