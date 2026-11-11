#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleFootIkRaycastCrash :
		public Module
	{
	public:
		ModuleFootIkRaycastCrash();
		virtual ~ModuleFootIkRaycastCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
