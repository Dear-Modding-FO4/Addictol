#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMovementPlanner :
		public Module
	{
	public:
		ModuleMovementPlanner();
		virtual ~ModuleMovementPlanner() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}