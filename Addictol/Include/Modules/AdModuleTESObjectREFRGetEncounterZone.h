#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleTESObjectREFRGetEncounterZone :
		public Module
	{
	public:
		ModuleTESObjectREFRGetEncounterZone();
		virtual ~ModuleTESObjectREFRGetEncounterZone() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}