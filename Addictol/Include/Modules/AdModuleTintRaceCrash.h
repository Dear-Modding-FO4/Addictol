#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleTintRaceCrash :
		public Module
	{
	public:
		ModuleTintRaceCrash();
		virtual ~ModuleTintRaceCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
