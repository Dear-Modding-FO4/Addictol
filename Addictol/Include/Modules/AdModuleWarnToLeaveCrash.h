#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleWarnToLeaveCrash :
		public Module
	{
	public:
		ModuleWarnToLeaveCrash();
		virtual ~ModuleWarnToLeaveCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
