#include <AdModuleManager.h>

namespace Addictol
{
	bool ModuleManager::SafeQueryMod(const Module* mod)
	{
		__try
		{
			return mod->DoQuery();
		}
		__except (1)
		{
			return false;
		}
	}

	bool ModuleManager::SafeInstallMod(Module* mod)
	{
		__try
		{
			return mod->DoInstall();
		}
		__except (1)
		{
			return false;
		}
	}

	bool ModuleManager::Register(Module* mod) noexcept
	{
		if (!mod)
		{
			REX::ERROR("" __FUNCTION__ ": mod is nullptr");
			return false;
		}

		auto nameModule = mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("" __FUNCTION__ ": The module is empty name");
			return false;
		}

		if (modules.find(nameModule) != modules.end())
		{
			REX::ERROR("" __FUNCTION__ ": The module must be unique name \"{}\"", nameModule);
			return false;
		}

		modules.insert({ mod->GetName(), mod });
		return true;
	}

	bool ModuleManager::Unregister(const Module* mod) noexcept
	{
		if (!mod)
		{
			REX::ERROR("" __FUNCTION__ ": mod is nullptr");
			return false;
		}

		auto nameModule = mod->GetName();
		if (nameModule.empty())
		{
			REX::ERROR("" __FUNCTION__ ": The module is empty name");
			return false;
		}

		auto it = modules.find(nameModule);
		if (it == modules.end())
		{
			REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", nameModule);
			return false;
		}

		modules.erase(it);
		return true;
	}

	bool ModuleManager::UnregisterByName(const char* name) noexcept
	{
		if (!name || !name[0])
		{
			REX::ERROR("" __FUNCTION__ ": name is nullptr/empty");
			return false;
		}

		std::string findName = name;
		strlwr(findName.data());

		auto it = modules.find(findName);
		if (it == modules.end())
		{
			REX::ERROR("" __FUNCTION__ ": The module no found \"{}\"", findName);
			return false;
		}

		modules.erase(it);
		return true;
	}

	void ModuleManager::QueryAll() noexcept
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
				REX::ERROR("Module \"{}\": failed verification", mod->GetName());
				needRemovedList.emplace_back(mod);
			}
		}

		for (auto m : needRemovedList)
			Unregister(m);
	}

	void ModuleManager::InstallAll() noexcept
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
}