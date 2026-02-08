#pragma once

#include <REX\REX\Singleton.h>
#include <resource_version2.h>
#include <F4SE/F4SE.h>
#include <AdModuleManager.h>

#define _PluginName      "Addictol"
#define _PluginAuthor    "Dear Modding FO4 Team"

namespace Addictol
{
	class Plugin :
		public REX::TSingleton<Plugin>
	{
		bool isInit{ false };
		bool isPreloadInit{ false };
		bool isInstall{ false };
		ModuleManager moduleManager;

		Plugin(const Plugin&) = delete;
		Plugin& operator=(const Plugin&) = delete;
	public:
		Plugin() = default;
		virtual ~Plugin() = default;

		[[nodiscard]] virtual bool Init(const F4SE::LoadInterface* a_f4se);
		[[nodiscard]] virtual bool PreloadInit(const F4SE::PreLoadInterface* a_preloadf4se);

		[[nodiscard]] virtual inline bool IsInit() const noexcept { return isInit; }
		[[nodiscard]] virtual inline bool IsInstall() const noexcept { return isInstall; }
		[[nodiscard]] virtual inline ModuleManager& GetModules() noexcept { return moduleManager; }

		inline void SetAsInstall() noexcept { isInstall = true; }
	};
}