#include <Core/AdAssert.h>
#include <Core/AdUtils.h>
#include <Core/AdModuleManager.h>
#include <Core/Settings/AdSetting.h>
#include <Telemetry/AdTelemetryHub.h>

#include <algorithm>
#include <tuple>

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

	namespace
	{
		[[nodiscard]] std::string_view ModuleStageName(
			ModuleManager::Type a_type) noexcept
		{
			if (a_type == ModuleManager::Type::kLoad)
				return "Load"sv;
			return g_msgName[std::to_underlying(a_type) - 1];
		}
	}

	bool ModuleManager::SafeQueryMod(const ModulePtr& a_mod) const
	{
		__try
		{
			return a_mod->DoQuery();
		}
		__except (1)
		{
			a_mod->ClearSkip();
			REX::ERROR("Module \"{}\": caught exception during query"sv, a_mod->GetName());
			return false;
		}
	}

	bool ModuleManager::SafeInstallMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg) const
	{
		__try
		{
			return a_mod->DoInstall(a_msg);
		}
		__except (1)
		{
			a_mod->ClearSkip();
			REX::ERROR("Module \"{}\": caught exception during install"sv, a_mod->GetName());
			return false;
		}
	}

	bool ModuleManager::SafeListenerMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg) const
	{
		__try
		{
			return a_mod->DoListener(a_msg);
		}
		__except (1)
		{
			REX::ERROR("Module \"{}\": caught exception during listener"sv, a_mod->GetName());
			return false;
		}
	}

	bool ModuleManager::SafeListenerPapyrusMod(const ModulePtr& a_mod, RE::BSScript::IVirtualMachine* a_vm) const
	{
		__try
		{
			return a_mod->DoPapyrusListener(a_vm);
		}
		__except (1)
		{
			REX::ERROR("Module \"{}\": caught exception during papyrus listener"sv, a_mod->GetName());
			return false;
		}
	}

	void ModuleManager::UnregisterPreloadAll() noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
		modules.clear();
	}

	ModuleManager::ModuleManager() :
		m_defender(std::make_unique<ModuleDefender>())
	{}

	bool ModuleManager::Register(const ModulePtr& a_mod, Type a_type) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
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
			if (modules.contains(nameModule))
			{
				REX::ERROR("{}: The module must be unique name \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules.try_emplace(a_mod->GetName(), a_mod);
		}
		else
		{
			auto msg_id = std::to_underlying(a_type) - 1;
			auto it = rl_modules.find(msg_id);
			if (it == rl_modules.end())
			{
				rl_modules.insert({ msg_id, {} });
				it = rl_modules.find(msg_id);
				AdAssert(it != rl_modules.end());
			}

			auto& modules_by_type = it->second;
			if (modules_by_type.contains(nameModule))
			{
				REX::ERROR("{}: The module must be unique name \"{}\""sv, __FUNCTION__, nameModule);
				return false;
			}

			modules_by_type.try_emplace(a_mod->GetName(), a_mod);
		}

		ModuleRegistrationStatus status{ a_mod, a_type };
		if (const auto* setting =
				SettingRegistry::GetSingleton().Find(a_mod->GetOption()))
		{
			status.settingSection = setting->Section();
			status.settingKey = setting->Key();
		}
		m_registrations.push_back(std::move(status));
		if (const auto source = std::dynamic_pointer_cast<MetricSource>(a_mod))
			(void)Telemetry::Hub().Register(source);
		return true;
	}

	bool ModuleManager::Unregister(const ModulePtr& a_mod, Type a_type) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
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
			auto msg_id = std::to_underlying(a_type) - 1;
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
		const std::scoped_lock lock{ m_modulesMutex };
		if (!a_name || !a_name[0])
		{
			REX::ERROR("{}: name is nullptr/empty"sv, __FUNCTION__);
			return false;
		}

		std::string findName = a_name;

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
			auto msg_id = std::to_underlying(a_type) - 1;
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
		const std::scoped_lock lock{ m_modulesMutex };
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
					RecordOutcome(mod, Type::kLoad, ModuleOutcome::kDisabled);
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled"sv, mod->GetName());
			}
			else
				REX::INFO("Module \"{}\": mandatory"sv, mod->GetName());

			mod->ClearSkip();
			if (!SafeQueryMod(mod))
			{
				if (mod->WasSkipped())
				{
					RecordOutcome(mod, Type::kLoad, ModuleOutcome::kSkipped);
					REX::INFO("Module \"{}\": skipped ({})"sv, mod->GetName(), mod->GetSkipReason());
				}
				else
				{
					RecordOutcome(mod, Type::kLoad, ModuleOutcome::kFailedQuery);
					REX::WARN("Module \"{}\": failed verification, the game version may not be supported"sv, mod->GetName());
				}

				needRemovedList.emplace_back(mod);
			}
		}

		for (auto& m : needRemovedList)
			modules.erase(m->GetName());
	}

	void ModuleManager::InstallLoadAll() noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
		(void)m_defender->Initialize();

		for (auto& it : modules)
		{
			auto& mod = it.second;

			if (m_defender && mod->HasProcessDefender())
				(void)m_defender->TakeSnapshot();

			mod->ClearSkip();
			if(!SafeInstallMod(mod))
			{
				if (mod->WasSkipped())
				{
					RecordOutcome(mod, Type::kLoad, ModuleOutcome::kSkipped);
					REX::INFO("Module \"{}\": skipped ({})"sv, mod->GetName(), mod->GetSkipReason());
				}
				else
				{
					RecordOutcome(mod, Type::kLoad, ModuleOutcome::kFailedInstall);
					REX::ERROR("Module \"{}\": fatal installation"sv, mod->GetName());
				}

				if (m_defender && mod->HasProcessDefender())
					(void)m_defender->RestoreFromSnapshot();
			}
			else
			{
				RecordOutcome(mod, Type::kLoad, ModuleOutcome::kInstalled);
				REX::INFO("Module \"{}\": installed"sv, mod->GetName());
			}
		}

		m_defender->Release();
	}

	void ModuleManager::ListenerLoadAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
		if (!a_msg)
			return;

		for (auto& it : modules)
		{
			auto& mod = it.second;
			if (mod->HasListener(a_msg->type))
			{
				if ((a_msg->type == F4SE::MessagingInterface::kPostLoadGame) && 
					!reinterpret_cast<uintptr_t>(a_msg->data))
				{
					// Data is a bool that is false if the game isn't actually loaded due to
					// missing mods or other messages
					continue;
				}

				if (!SafeListenerMod(mod, a_msg))
					REX::ERROR("Module \"{}\": fatal listener (msg_type: {})"sv, mod->GetName(), g_msgName[a_msg->type]);
			}
		}
	}

	void ModuleManager::QueryAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
		if (!a_msg)
			return;

		auto it = rl_modules.find((uint8_t)(a_msg->type));
		if (it == rl_modules.end())
			return;

		auto& modules_by_type = it->second;
		const auto moduleType = static_cast<Type>(a_msg->type + 1);
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
					RecordOutcome(mod, moduleType, ModuleOutcome::kDisabled);
					needRemovedList.emplace_back(mod);
					continue;
				}
				else
					REX::INFO("Module \"{}\": enabled by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
			}
			else
				REX::INFO("Module \"{}\": mandatory by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);

			mod->ClearSkip();
			if (!SafeQueryMod(mod))
			{
				if (mod->WasSkipped())
				{
					RecordOutcome(mod, moduleType, ModuleOutcome::kSkipped);
					REX::INFO("Module \"{}\": skipped by message {} ({})"sv, mod->GetName(), g_msgName[a_msg->type], mod->GetSkipReason());
				}
				else
				{
					RecordOutcome(mod, moduleType, ModuleOutcome::kFailedQuery);
					REX::ERROR("Module \"{}\": failed verification by message {}, the game version may not be supported"sv,
						mod->GetName(), g_msgName[a_msg->type]);
				}

				needRemovedList.emplace_back(mod);
			}
		}

		for (auto& m : needRemovedList)
			modules_by_type.erase(m->GetName());
		if (modules_by_type.empty())
			rl_modules.erase(it);
	}

	void ModuleManager::InstallAllByMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
		if (!a_msg)
			return;

		auto it = rl_modules.find((uint8_t)(a_msg->type));
		if (it == rl_modules.end())
			return;

		if ((a_msg->type == F4SE::MessagingInterface::kGameDataReady) &&
			!reinterpret_cast<uintptr_t>(a_msg->data))
		{
			// Data is a bool that is false if the game isn't actually loaded due to
			// missing mods or other messages
			return;
		}		

		(void)m_defender->Initialize();

		auto& modules_by_type = it->second;
		const auto moduleType = static_cast<Type>(a_msg->type + 1);
		for (auto& it : modules_by_type)
		{
			auto& mod = it.second;

			if (m_defender && mod->HasProcessDefender())
				(void)m_defender->TakeSnapshot();

			mod->ClearSkip();
			if (!SafeInstallMod(mod, a_msg))
			{
				if (mod->WasSkipped())
				{
					RecordOutcome(mod, moduleType, ModuleOutcome::kSkipped);
					REX::INFO("Module \"{}\": skipped by message {} ({})"sv, mod->GetName(), g_msgName[a_msg->type], mod->GetSkipReason());
				}
				else
				{
					RecordOutcome(mod, moduleType, ModuleOutcome::kFailedInstall);
					REX::ERROR("Module \"{}\": fatal installation by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
				}

				if (m_defender && mod->HasProcessDefender())
					(void)m_defender->RestoreFromSnapshot();
			}
			else
			{
				RecordOutcome(mod, moduleType, ModuleOutcome::kInstalled);
				REX::INFO("Module \"{}\": installed by message {}"sv, mod->GetName(), g_msgName[a_msg->type]);
			}
		}

		m_defender->Release();
	}

	void ModuleManager::ListenerAllPapyrus(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		const std::scoped_lock lock{ m_modulesMutex };
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

	void ModuleManager::RecordOutcome(
		const ModulePtr& a_mod,
		Type a_type,
		ModuleOutcome a_outcome) noexcept
	{
		const auto position = std::find_if(
			m_registrations.rbegin(),
			m_registrations.rend(),
			[&](const ModuleRegistrationStatus& a_status) {
				return a_status.module == a_mod && a_status.type == a_type;
			});
		if (position == m_registrations.rend())
		{
			REX::ERROR("Module \"{}\": outcome has no registration"sv, a_mod->GetName());
			return;
		}

		a_mod->SetOutcome(a_outcome);
		position->skipReason =
			a_outcome == ModuleOutcome::kSkipped ?
				std::string{ a_mod->GetSkipReason() } :
				std::string{};

		ModuleOutcomeTally tally{
			m_installed.load(std::memory_order_relaxed),
			m_disabled.load(std::memory_order_relaxed),
			m_skipped.load(std::memory_order_relaxed),
			m_failedQuery.load(std::memory_order_relaxed),
			m_failedInstall.load(std::memory_order_relaxed)
		};
		RecordModuleOutcome(position->outcome, tally, a_outcome);
		m_installed.store(tally[0], std::memory_order_relaxed);
		m_disabled.store(tally[1], std::memory_order_relaxed);
		m_skipped.store(tally[2], std::memory_order_relaxed);
		m_failedQuery.store(tally[3], std::memory_order_relaxed);
		m_failedInstall.store(tally[4], std::memory_order_relaxed);
	}

	std::vector<ModuleStatusSnapshot> ModuleManager::ModuleStatuses() const
	{
		const std::scoped_lock lock{ m_modulesMutex };
		std::map<std::string_view, size_t> registrationsByName;
		for (const auto& registration : m_registrations)
			++registrationsByName[registration.module->GetName()];

		std::vector<ModuleStatusSnapshot> statuses;
		statuses.reserve(m_registrations.size());
		for (const auto& registration : m_registrations)
		{
			const auto name = registration.module->GetName();
			statuses.push_back({
				std::string{ name },
				registration.outcome,
				registration.skipReason,
				registration.settingSection,
				registration.settingKey,
				registrationsByName[name] > 1 ?
					std::string{ ModuleStageName(registration.type) } :
					std::string{}
			});
		}
		std::ranges::sort(
			statuses,
			[](const ModuleStatusSnapshot& a_left,
				const ModuleStatusSnapshot& a_right) {
				return std::tie(a_left.name, a_left.stage) <
					std::tie(a_right.name, a_right.stage);
			});
		return statuses;
	}

	void ModuleManager::LogSummary() const noexcept
	{
		const auto outcomes = ModuleOutcomeCounts();
		REX::INFO("Module Summary: {} installed, {} disabled, {} skipped, {} failed query, {} failed install ({} total)"sv,
			outcomes[0], outcomes[1], outcomes[2], outcomes[3], outcomes[4],
			outcomes[0] + outcomes[1] + outcomes[2] + outcomes[3] + outcomes[4]);
	}
}