#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleIOCacher :
		public Module
	{
	public:
		ModuleIOCacher();
		virtual ~ModuleIOCacher() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept override;
	};
}