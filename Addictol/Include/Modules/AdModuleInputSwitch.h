#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleInputSwitch :
		public Module
	{
	public:
		ModuleInputSwitch();
		virtual ~ModuleInputSwitch() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept override;
	};
}