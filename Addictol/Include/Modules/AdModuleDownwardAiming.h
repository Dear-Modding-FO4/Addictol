#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleDownwardAiming :
		public Module
	{
	public:
		ModuleDownwardAiming();
		virtual ~ModuleDownwardAiming() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}