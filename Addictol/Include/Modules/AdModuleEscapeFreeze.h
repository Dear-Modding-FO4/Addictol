#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleEscapeFreeze :
		public Module
	{
	public:
		ModuleEscapeFreeze();
		virtual ~ModuleEscapeFreeze() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
