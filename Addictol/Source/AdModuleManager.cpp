#include <AdAssert.h>
#include <AdUtils.h>
#include <AdModuleManager.h>

namespace Addictol
{
	using namespace std::literals;

	std::array<std::string_view, 11> g_msgName
	{
		"kPostLoad",
		"kPostPostLoad",
		"kPreLoadGame",
		"kPostLoadGame",
		"kPreSaveGame",
		"kPostSaveGame",
		"kDeleteGame",
		"kInputLoaded",
		"kNewGame",
		"kGameLoaded",
		"kGameDataReady"
	};

	bool ModuleManager::SafeQueryMod(const ModulePtr& a_mod)
	{
		__try
		{
			return a_mod->DoQuery();
		}
		__except (1)
		{
			return false;
		}
	}

	bool ModuleManager::SafeInstallMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg)
	{
		__try
		{
			return a_mod->DoInstall(a_msg);
		}
		__except (1)
		{
			return false;
		}
	}

	bool ModuleManager::SafeListenerMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg)
	{
		__try
		{
			return a_mod->DoListener(a_msg);
		}
		__except (1)
		{
			return false;
		}
	}

	bool ModuleManager::SafeListenerPapyrusMod(const ModulePtr& a_mod, RE::BSScript::IVirtualMachine* a_vm)
	{
		__try
		{
			return a_mod->DoPapyrusListener(a_vm);
		}
		__except (1)
		{
			return false;
		}
	}

	void ModuleManager::UnregisterPreloadAll() noexcept
	{
		modules.clear();
	}

	bool ModuleManager::Register(const ModulePtr& a_mod, Type a_type) noexcept
	{
		if (!a_mod)
		{
			REX::ERROR("{}: a_mod is nullptr"sv, __FUNCTION__);
			return false;
		}

		auto nameModule = a_mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("{}: The module is empty name"sv, __FUNCTION__);
			return false;
		}

		if (a_type == Type::kLoad)
		{
			if (modules.find(nameModule) != modules.end())
			{
				REX::ERROR("{}: The module must be unique name \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules.insert({ a_mod->GetName(), a_mod });
			return true;
		}
		else
		{
			auto msg_id = static_cast<uint8_t>(a_type) - 1;
			auto it = rl_modules.find(msg_id);
			if (it == rl_modules.end())
			{
				rl_modules.insert({ msg_id, {} });
				it = rl_modules.find(msg_id);
				AdAssert(it != rl_modules.end());
			}

			auto& modules_by_type = it->second;
			if (modules_by_type.find(nameModule) != modules_by_type.end())
			{
				REX::ERROR("{}: The module must be unique name \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules_by_type.insert({ a_mod->GetName(), a_mod });
			return true;
		}
	}

	bool ModuleManager::Unregister(const ModulePtr& a_mod, Type a_type) noexcept
	{
		if (!a_mod)
		{
			REX::ERROR("{}: mod is nullptr"sv, __FUNCTION__);
			return false;
		}

		auto nameModule = a_mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("{}: The module is empty name"sv, __FUNCTION__);
			return false;
		}

		if (a_type == Type::kLoad)
		{
			auto it = modules.find(nameModule);
			if (it == modules.end())
			{
				REX::ERROR("{}: The module no found \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules.erase(it);
			return true;
		}
		else
		{
			auto msg_id = static_cast<uint8_t>(a_type) - 1;
			auto it = rl_modules.find(msg_id);
			if (it == rl_modules.end())
			{
				REX::ERROR("{}: No list of modules of this type \"{}\" has been found."sv, 
					__FUNCTION__, g_msgName[msg_id]);
				return false;
			}

			auto& modules_by_type = it->second;
			auto it2 = modules_by_type.find(nameModule);
			if (it2 == modules_by_type.end())
			{
				REX::ERROR("{}: The module no found \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules_by_type.erase(it2);
			if (!modules_by_type.size())
				rl_modules.erase(it);

			return true;
		}
	}

	bool ModuleManager::UnregisterByName(const char* a_name, Type a_type) noexcept
	{
		if (!a_name || !a_name[0])
		{
			REX::ERROR("{}: name is nullptr/empty"sv, __FUNCTION__);
			return false;
		}

		std::string findName = a_name;
		strlwr(findName.data());

		if (a_type == Type::kLoad)
		{
			auto it = modules.find(findName);
			if (it == modules.end())
			{
				REX::ERROR("{}: The module no found \"{}\""sv, __FUNCTION__, findName);
				return false;
			}

			modules.erase(it);
			return true;
		}
		else
		{
			auto msg_id = static_cast<uint8_t>(a_type) - 1;
			auto it = rl_modules.find(msg_id);
			if (it == rl_modules.end())
			{
				REX::ERROR("{}: No list of modules of this type \"{}\" has been found."sv, __FUNCTION__, msg_id);
				return false;
			}

			auto& modules_by_type = it->second;
			auto it2 = modules_by_type.find(findName);
			if (it2 == modules_by_type.end())
			{
				REX::ERROR("{}: The module no found \"{}\""sv, __FUNCTION__, findName);
				return false;
			}

			modules_by_type.erase(it2);
			if (!modules_by_type.size())
				rl_modules.erase(it);

			return true;
		}
	}

	void ModuleManager::InstallPreloadAll() noexcept
	{
		InstallLoadAll();
		UnregisterPreloadAll();
	}

	void ModuleManager::QueryLoadAll() noexcept
	{
		std::vector<ModulePtr> needRemovedList;

		for (auto it = modules.begin(); it != modules.end(); it++)
		{
			auto& mod = it->second;
			if (!mod)
			{
				REX::ERROR("{}: mod is nullptr"sv, __FUNCTION__);
				continue;
			}

			const auto optionName = it->second->GetOption();
			if (optionName)
			{
				if (!optionName->GetValue())
				{
					REX::INFO("Module \"{}\": disabled"sv, mod->GetName());
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled"sv, mod->GetName());
			}
			else
				REX::INFO("Module \"{}\": mandatory"sv, mod->GetName());

			if (!SafeQueryMod(mod))
			{
				REX::WARN("Module \"{}\": failed verification, the game version may not be supported"sv, mod->GetName());
				needRemovedList.emplace_back(mod);
			}
		}

		for (auto& m : needRemovedList)
			Unregister(m);
	}

	void ModuleManager::InstallLoadAll() noexcept
	{
		for (auto& it : modules)
		{
			auto& mod = it.second;
			if(!SafeInstallMod(mod))
				REX::ERROR("Module \"{}\": fatal installation"sv, mod->GetName());
			else
				REX::INFO("Module \"{}\": installed"sv, mod->GetName());
		}
	}

	void ModuleManager::ListenerLoadAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return;

		for (auto& it : modules)
		{
			auto& mod = it.second;
			if (mod->HasListener(a_msg->type))
				if (!SafeListenerMod(mod, a_msg))
					REX::ERROR("Module \"{}\": fatal listener (msg_type: {})"sv, mod->GetName(), g_msgName[a_msg->type]);
		}
	}

	void ModuleManager::QueryAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return;

		auto it = rl_modules.find((uint8_t)(a_msg->type));
		if (it == rl_modules.end())
			return;

		auto& modules_by_type = it->second;
		std::vector<ModulePtr> needRemovedList;

		for (auto it = modules_by_type.begin(); it != modules_by_type.end(); it++)
		{
			auto& mod = it->second;
			if (!mod)
			{
				REX::ERROR("{}: mod is nullptr"sv, __FUNCTION__);
				continue;
			}

			const auto optionName = it->second->GetOption();
			if (optionName)
			{
				if (!optionName->GetValue())
				{
					REX::INFO("Module \"{}\": disabled by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
			}
			else
				REX::INFO("Module \"{}\": mandatory by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);

			if (!SafeQueryMod(mod))
			{
				REX::ERROR("Module \"{}\": failed verification by message {}, the game version may not be supported",
					mod->GetName(), g_msgName[a_msg->type]);
				needRemovedList.emplace_back(mod);
			}
		}

		for (auto& m : needRemovedList)
			Unregister(m, (Type)(a_msg->type + 1));
	}

	void ModuleManager::InstallAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return;

		auto it = rl_modules.find((uint8_t)(a_msg->type));
		if (it == rl_modules.end())
			return;

		auto& modules_by_type = it->second;
		for (auto& it : modules_by_type)
		{
			auto& mod = it.second;
			if (!SafeInstallMod(mod, a_msg))
				REX::ERROR("Module \"{}\": fatal installation by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
			else
				REX::INFO("Module \"{}\": installed by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
		}
	}

	void ModuleManager::ListenerAllPapyrus(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		if (!a_vm)
			return;

		for (auto& it : modules)
		{
			auto& mod = it.second;
			if (!mod->HasPapyrusListener())
				continue;

			if (!SafeListenerPapyrusMod(mod, a_vm))
				REX::ERROR("Module \"{}\": fatal papyrus installation"sv, mod->GetName());
			else
				REX::INFO("Module \"{}\": papyrus installed"sv, mod->GetName());
		}
	}
}