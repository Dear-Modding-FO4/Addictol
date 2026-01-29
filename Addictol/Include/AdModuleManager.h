#pragma once

#include <AdModule.h>
#include <map>
#include <string_view>

namespace Addictol
{
	class ModuleManager
	{
		std::map<std::string_view, Module*> modules;

		ModuleManager(const ModuleManager&) = delete;
		ModuleManager operator=(const ModuleManager&) = delete;

		[[nodiscard]] bool SafeQueryMod(const Module* mod);
		[[nodiscard]] bool SafeInstallMod(Module* mod);
	public:
		ModuleManager() = default;
		virtual ~ModuleManager() = default;

		virtual bool Register(Module* mod) noexcept;
		virtual bool Unregister(const Module* mod) noexcept;
		virtual bool UnregisterByName(const char* name) noexcept;

		virtual void QueryAll() noexcept;
		virtual void InstallAll() noexcept;
	};
}