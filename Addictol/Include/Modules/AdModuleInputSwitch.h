#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	namespace inputSwitchDetail
	{
		[[nodiscard]] constexpr bool ShouldClearKeyboardMouseIgnore(bool a_menuVisible) noexcept
		{
			return !a_menuVisible;
		}
	}

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