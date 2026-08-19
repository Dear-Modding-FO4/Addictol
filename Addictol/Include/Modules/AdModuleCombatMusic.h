#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleCombatMusic :
		public Module
	{
	public:
		ModuleCombatMusic();
		virtual ~ModuleCombatMusic() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
