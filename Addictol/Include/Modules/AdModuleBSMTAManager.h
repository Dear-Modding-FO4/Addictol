#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleBSMTAManager :
		public Module
	{
	public:
		ModuleBSMTAManager();
		virtual ~ModuleBSMTAManager() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}