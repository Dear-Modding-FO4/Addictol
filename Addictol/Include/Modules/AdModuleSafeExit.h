#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleSafeExit :
		public Module
	{
	public:
		ModuleSafeExit();
		virtual ~ModuleSafeExit() = default;

		[[nodiscard]] static bool IsEnabledInConfig() noexcept;
		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept override;
	};
}