#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleMaxStdIO :
		public Module
	{
	public:
		ModuleMaxStdIO();
		virtual ~ModuleMaxStdIO() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}