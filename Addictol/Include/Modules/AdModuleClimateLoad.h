#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleClimateLoadFix :
		public Module
	{
	public:
		ModuleClimateLoadFix();
		virtual ~ModuleClimateLoadFix() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
