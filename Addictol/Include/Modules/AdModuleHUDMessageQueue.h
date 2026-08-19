#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleHUDMessageQueue :
		public Module
	{
	public:
		ModuleHUDMessageQueue();
		virtual ~ModuleHUDMessageQueue() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}