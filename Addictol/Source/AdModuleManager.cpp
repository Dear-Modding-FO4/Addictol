#include <AdAssert.h>
#include <AdModuleManager.h>

namespace Addictol
{
	bool ModuleManager::SafeQueryMod(const Module* a_mod)
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

	bool ModuleManager::SafeInstallMod(Module* a_mod, F4SE::MessagingInterface::Message* a_msg)
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

	bool ModuleManager::SafeListenerMod(Module* a_mod, F4SE::MessagingInterface::Message* a_msg)
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

	bool ModuleManager::Register(Module* a_mod, Type a_type) noexcept
	{
		if (!a_mod)
		{
			REX::ERROR("" __FUNCTION__ ": a_mod is nullptr");
			return false;
		}

		auto nameModule = a_mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("" __FUNCTION__ ": The module is empty name");
			return false;
		}

		if (a_type == Type::kPreload)
		{
			if (modules.find(nameModule) != modules.end())
			{
				REX::ERROR("" __FUNCTION__ ": The module must be unique name \"{}\"", nameModule);
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
				REX::ERROR("" __FUNCTION__ ": The module must be unique name \"{}\"", nameModule);
				return false;
			}

			modules_by_type.insert({ a_mod->GetName(), a_mod });
			return true;
		}
	}

	bool ModuleManager::Unregister(const Module* a_mod, Type a_type) noexcept
	{
		if (!a_mod)
		{
			REX::ERROR("" __FUNCTION__ ": mod is nullptr");
			return false;
		}

		auto nameModule = a_mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("" __FUNCTION__ ": The module is empty name");
			return false;
		}

		if (a_type == Type::kPreload)
		{
			auto it = modules.find(nameModule);
			if (it == modules.end())
			{
				REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", nameModule);
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
				REX::ERROR("" __FUNCTION__ ": No list of modules of this type \"{}\" has been found.", msg_id);
				return false;
			}

			auto& modules_by_type = it->second;
			auto it2 = modules_by_type.find(nameModule);
			if (it2 == modules_by_type.end())
			{
				REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", nameModule);
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
			REX::ERROR("" __FUNCTION__ ": name is nullptr/empty");
			return false;
		}

		std::string findName = a_name;
		strlwr(findName.data());

		if (a_type == Type::kPreload)
		{
			auto it = modules.find(findName);
			if (it == modules.end())
			{
				REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", findName);
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
				REX::ERROR("" __FUNCTION__ ": No list of modules of this type \"{}\" has been found.", msg_id);
				return false;
			}

			auto& modules_by_type = it->second;
			auto it2 = modules_by_type.find(findName);
			if (it2 == modules_by_type.end())
			{
				REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", findName);
				return false;
			}

			modules_by_type.erase(it2);
			if (!modules_by_type.size())
				rl_modules.erase(it);

			return true;
		}
	}

	void ModuleManager::QueryPreloadAll() noexcept
	{
		std::vector<const Module*> needRemovedList;

		for (auto it = modules.begin(); it != modules.end(); it++)
		{
			auto mod = it->second;
			if (!mod)
			{
				REX::ERROR("" __FUNCTION__ ": mod is nullptr");
				continue;
			}

			const auto optionName = it->second->GetOption();
			if (optionName)
			{
				if (!optionName->GetValue())
				{
					REX::INFO("Module \"{}\": disabled", mod->GetName());
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled", mod->GetName());
			}

			if (!SafeQueryMod(mod))
			{
				REX::WARN("Module \"{}\": failed verification, the game version may not be supported", mod->GetName());
				needRemovedList.emplace_back(mod);
			}
		}

		for (auto m : needRemovedList)
			Unregister(m);
	}

	void ModuleManager::InstallPreloadAll() noexcept
	{
		for (auto& it : modules)
		{
			auto mod = it.second;
			if(!SafeInstallMod(mod))
				REX::ERROR("Module \"{}\": fatal installation", mod->GetName());
			else
				REX::INFO("Module \"{}\": installed", mod->GetName());
		}
	}

	void ModuleManager::ListenerPreloadAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return;

		for (auto& it : modules)
		{
			auto mod = it.second;
			if (mod->HasListener(a_msg->type))
				if (!SafeListenerMod(mod, a_msg))
					REX::ERROR("Module \"{}\": fatal listener (msg_type: {})", mod->GetName(), a_msg->type);
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
		std::vector<const Module*> needRemovedList;

		for (auto it = modules_by_type.begin(); it != modules_by_type.end(); it++)
		{
			auto mod = it->second;
			if (!mod)
			{
				REX::ERROR("" __FUNCTION__ ": mod is nullptr");
				continue;
			}

			const auto optionName = it->second->GetOption();
			if (optionName)
			{
				if (!optionName->GetValue())
				{
					REX::INFO("Module \"{}\": disabled", mod->GetName());
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled", mod->GetName());
			}

			if (!SafeQueryMod(mod))
			{
				REX::ERROR("Module \"{}\": failed verification, the game version may not be supported", mod->GetName());
				needRemovedList.emplace_back(mod);
			}
		}

		for (auto m : needRemovedList)
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
			auto mod = it.second;
			if (!SafeInstallMod(mod, a_msg))
				REX::ERROR("Module \"{}\": fatal installation", mod->GetName());
			else
				REX::INFO("Module \"{}\": installed", mod->GetName());
		}
	}
}