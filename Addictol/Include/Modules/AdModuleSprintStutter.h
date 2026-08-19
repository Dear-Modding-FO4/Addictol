#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleSprintStutter :
		public Module
	{
	public:
		ModuleSprintStutter();
		virtual ~ModuleSprintStutter() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
