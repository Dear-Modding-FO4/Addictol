#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleEffectItemConditions :
		public Module
	{
	public:
		ModuleEffectItemConditions();
		virtual ~ModuleEffectItemConditions() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
