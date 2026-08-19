#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModulePapyrusGC :
		public Module
	{
	public:
		ModulePapyrusGC();
		virtual ~ModulePapyrusGC() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
