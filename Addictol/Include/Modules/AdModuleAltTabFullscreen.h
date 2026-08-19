#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleAltTabFullscreen :
		public Module
	{
	public:
		ModuleAltTabFullscreen();
		virtual ~ModuleAltTabFullscreen() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
