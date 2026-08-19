#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleMovementPlanner :
		public Module
	{
	public:
		ModuleMovementPlanner();
		virtual ~ModuleMovementPlanner() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}