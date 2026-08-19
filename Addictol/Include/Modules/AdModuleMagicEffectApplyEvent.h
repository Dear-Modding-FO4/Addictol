#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMagicEffectApplyEvent :
		public Module
	{
	public:
		ModuleMagicEffectApplyEvent();
		virtual ~ModuleMagicEffectApplyEvent() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}