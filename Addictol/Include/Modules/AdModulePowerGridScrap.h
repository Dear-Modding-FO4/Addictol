#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModulePowerGridScrap :
		public Module
	{
	public:
		ModulePowerGridScrap();
		virtual ~ModulePowerGridScrap() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
