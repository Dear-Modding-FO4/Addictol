#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModulePapyrusLeaks :
		public Module
	{
	public:
		ModulePapyrusLeaks();
		virtual ~ModulePapyrusLeaks() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
