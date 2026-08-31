#pragma once

#include <Core/AdModule.h>
#include <Core/AdModuleDefender.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol
{
	struct ModuleStatusSnapshot
	{
		std::string name;
		ModuleOutcome outcome{ ModuleOutcome::kPending };
		std::string skipReason;
		std::string settingSection;
		std::string settingKey;
		std::string stage;
	};

	class ModuleManager
	{
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

	private:
		using ModulePtr = std::shared_ptr<Module>;

		struct ModuleRegistrationStatus
		{
			ModulePtr module;
			Type type{ Type::kLoad };
			ModuleOutcome outcome{ ModuleOutcome::kPending };
			std::string skipReason;
			std::string settingSection;
			std::string settingKey;
		};

		std::map<std::string_view, ModulePtr> modules{};
		std::map<uint8_t, std::map<std::string_view, ModulePtr>> rl_modules{};
		std::vector<ModuleRegistrationStatus> m_registrations;
		std::unique_ptr<ModuleDefender> m_defender{};
		mutable std::mutex m_modulesMutex;

		std::atomic<uint64_t> m_disabled{ 0 };
		std::atomic<uint64_t> m_failedQuery{ 0 };
		std::atomic<uint64_t> m_installed{ 0 };
		std::atomic<uint64_t> m_failedInstall{ 0 };
		std::atomic<uint64_t> m_skipped{ 0 };

		ModuleManager(const ModuleManager&) = delete;
		ModuleManager(ModuleManager&&) = delete;
		ModuleManager operator=(ModuleManager&&) = delete;
		ModuleManager operator=(const ModuleManager&) = delete;

		[[nodiscard]] bool SafeQueryMod(const ModulePtr& a_mod) const;
		[[nodiscard]] bool SafeInstallMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg = nullptr) const;
		[[nodiscard]] bool SafeListenerMod(const ModulePtr& a_mod, F4SE::MessagingInterface::Message* a_msg = nullptr) const;
		[[nodiscard]] bool SafeListenerPapyrusMod(const ModulePtr& a_mod, RE::BSScript::IVirtualMachine* a_vm) const;
		void RecordOutcome(
			const ModulePtr& a_mod,
			Type a_type,
			ModuleOutcome a_outcome) noexcept;
		void UnregisterPreloadAll() noexcept;
	public:
		ModuleManager();
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
		[[nodiscard]] ModuleOutcomeTally ModuleOutcomeCounts() const noexcept
		{
			const std::scoped_lock lock{ m_modulesMutex };
			return {
				m_installed.load(std::memory_order_relaxed), m_disabled.load(std::memory_order_relaxed),
				m_skipped.load(std::memory_order_relaxed), m_failedQuery.load(std::memory_order_relaxed),
				m_failedInstall.load(std::memory_order_relaxed)
			};
		}
		[[nodiscard]] std::vector<ModuleStatusSnapshot> ModuleStatuses() const;
		virtual void LogSummary() const noexcept;
	};
}