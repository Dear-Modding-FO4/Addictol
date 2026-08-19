#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleBethesdaNetCrash :
		public Module
	{
	public:
		ModuleBethesdaNetCrash();
		virtual ~ModuleBethesdaNetCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
