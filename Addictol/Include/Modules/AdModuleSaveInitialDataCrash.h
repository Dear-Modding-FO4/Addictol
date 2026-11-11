#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleSaveInitialDataCrash :
		public Module
	{
	public:
		ModuleSaveInitialDataCrash();
		virtual ~ModuleSaveInitialDataCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
