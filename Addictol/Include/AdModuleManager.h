#pragma once

#include <AdModule.h>
#include <string_view>
#include <map>

namespace Addictol
{
	class ModuleManager
	{
		using ModulePtr = std::shared_ptr<Module>;
		std::map<std::string_view, ModulePtr> modules;
		std::map<uint8_t, std::map<std::string_view, ModulePtr>> rl_modules;

		ModuleManager(const ModuleManager&) = delete;
		ModuleManager operator=(const ModuleManager&) = delete;

		[[nodiscard]] bool SafeQueryMod(const ModulePtr& a_mod);
		[[nodiscard]] bool SafeInstallMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg = nullptr);
		[[nodiscard]] bool SafeListenerMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg = nullptr);
		[[nodiscard]] bool SafeListenerPapyrusMod(const ModulePtr& a_mod, RE::BSScript::IVirtualMachine* a_vm);
		void UnregisterPreloadAll() noexcept;
	public:
		enum class Type : uint8_t
		{
			kLoad = 0,
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

		virtual bool Register(const ModulePtr& a_mod, Type a_type = Type::kLoad) noexcept;
		virtual bool Unregister(const ModulePtr& a_mod, Type a_type = Type::kLoad) noexcept;
		virtual bool UnregisterByName(const char* a_name, Type a_type = Type::kLoad) noexcept;
		
		virtual inline void QueryPreloadAll() noexcept { QueryLoadAll(); }
		virtual void InstallPreloadAll() noexcept;

		virtual void QueryLoadAll() noexcept;
		virtual void InstallLoadAll() noexcept;
		virtual void ListenerLoadAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
		virtual void QueryAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
		virtual void InstallAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
		virtual void ListenerAllPapyrus(RE::BSScript::IVirtualMachine* a_vm) noexcept;
	};
}