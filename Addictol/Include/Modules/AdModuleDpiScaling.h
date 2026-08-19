#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleDpiScaling :
		public Module
	{
	public:
		ModuleDpiScaling();
		virtual ~ModuleDpiScaling() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
