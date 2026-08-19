#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleRobCoPatcherCache :
		public Module
	{
	public:
		ModuleRobCoPatcherCache();
		virtual ~ModuleRobCoPatcherCache() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
