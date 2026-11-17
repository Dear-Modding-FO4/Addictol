#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleFileCacheFreeze :
		public Module
	{
	public:
		ModuleFileCacheFreeze();
		virtual ~ModuleFileCacheFreeze() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
