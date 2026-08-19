#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleThreads :
		public Module
	{
	public:
		ModuleThreads();
		virtual ~ModuleThreads() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}