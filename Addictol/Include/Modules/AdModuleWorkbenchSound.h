#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleWorkbenchSound :
		public Module
	{
	public:
		ModuleWorkbenchSound();
		virtual ~ModuleWorkbenchSound() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
