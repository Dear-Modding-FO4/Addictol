#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMemoryManager :
		public Module
	{
	public:
		ModuleMemoryManager();
		virtual ~ModuleMemoryManager() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
	};
}