#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleEncounterZoneReset :
		public Module
	{
	public:
		ModuleEncounterZoneReset();
		virtual ~ModuleEncounterZoneReset() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}