#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleActorIsHostileToActor :
		public Module
	{
	public:
		ModuleActorIsHostileToActor();
		virtual ~ModuleActorIsHostileToActor() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}