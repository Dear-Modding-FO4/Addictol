#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleActorCauseSaveBloat :
		public Module
	{
	public:
		ModuleActorCauseSaveBloat();
		virtual ~ModuleActorCauseSaveBloat() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}