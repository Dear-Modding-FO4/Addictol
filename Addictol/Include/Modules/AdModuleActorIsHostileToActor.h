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

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}