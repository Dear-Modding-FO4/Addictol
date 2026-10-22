#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleExtraDataNullCrash :
		public Module
	{
	public:
		ModuleExtraDataNullCrash();
		virtual ~ModuleExtraDataNullCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
