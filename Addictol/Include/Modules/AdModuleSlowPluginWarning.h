#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	// Summarizes slow startup handlers; the hooks live in ModulePluginTiming.
	class ModuleSlowPluginWarning :
		public Module
	{
	public:
		ModuleSlowPluginWarning();
		virtual ~ModuleSlowPluginWarning() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
