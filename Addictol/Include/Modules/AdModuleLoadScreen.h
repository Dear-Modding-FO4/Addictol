#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleLoadScreen :
		public Module
	{
	public:
		ModuleLoadScreen();
		virtual ~ModuleLoadScreen() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}