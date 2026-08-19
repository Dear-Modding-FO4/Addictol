#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleWeaponDebrisCrash :
		public Module
	{
	public:
		ModuleWeaponDebrisCrash();
		virtual ~ModuleWeaponDebrisCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
