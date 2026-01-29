#pragma once

#include <REX\REX\Singleton.h>
#include <resource_version2.h>
#include <F4SE/F4SE.h>

#define _PluginName      "Addictol"
#define _PluginAuthor    "Dear Modding FO4 Team"

namespace Addictol
{
	class Plugin :
		public REX::TSingleton<Plugin>
	{
		bool isInit{ false };
		Plugin(const Plugin&) = delete;
		Plugin& operator=(const Plugin&) = delete;
	public:
		Plugin() = default;
		virtual ~Plugin() = default;

		[[nodiscard]] virtual bool Init(const F4SE::LoadInterface* a_f4se);
	};
}