#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMuzzleFlashLight :
		public Module
	{
	public:
		ModuleMuzzleFlashLight();
		virtual ~ModuleMuzzleFlashLight() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
