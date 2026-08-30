#pragma once

#include <Core/AdModuleOutcome.h>
#include <REX/REX.h>
#include <F4SE/F4SE.h>
#include <string_view>
#include <string>
#include <initializer_list>

namespace Addictol
{
	class ModuleManager;

	class Module :
		public REX::TSingleton<Module>
	{
		friend class ModuleManager;

		std::string_view name{};
		const REX::TOML::Bool<>* option{ nullptr };
		std::bitset<F4SE::MessagingInterface::kGameDataReady + 1> listener_messages;
		bool papyrusListener{ false };
		mutable bool skipped{ false };
		mutable std::string skipReason{};
		mutable ModuleOutcome outcome{ ModuleOutcome::kPending };

		Module(const Module&) = delete;
		Module& operator=(const Module&) = delete;
		void SetOutcome(ModuleOutcome a_outcome) const noexcept { outcome = a_outcome; }
	public:
		Module(const char* a_name, const REX::TOML::Bool<>* a_option = nullptr, 
			std::initializer_list<uint32_t> a_listeners = {}, bool a_papyrusListener = false);
		virtual ~Module() = default;

		[[nodiscard]] virtual std::string_view GetName() const noexcept { return name; }
		[[nodiscard]] virtual const REX::TOML::Bool<>* GetOption() const noexcept { return option; }
		[[nodiscard]] ModuleOutcome GetOutcome() const noexcept { return outcome; }

		[[nodiscard]] virtual bool DoQuery() const noexcept { return true; }
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept = 0;
		[[nodiscard]] virtual bool DoListener(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept { return true; }
		[[nodiscard]] virtual bool DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept { return true; }
		[[nodiscard]] virtual bool HasListener(uint32_t a_msgType) noexcept;
		[[nodiscard]] virtual bool HasPapyrusListener() noexcept;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept;

		// Declare a deliberate no-op: return false from DoQuery/DoInstall right after, and the
		// manager reports it as skipped rather than a failure.
		void Skip(std::string_view a_reason) const noexcept;
		void ClearSkip() const noexcept;
		[[nodiscard]] bool WasSkipped() const noexcept { return skipped; }
		[[nodiscard]] std::string_view GetSkipReason() const noexcept { return skipReason; }
	};
}