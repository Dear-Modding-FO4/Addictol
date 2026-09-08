#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleArmorPenetration :
		public Module
	{
	public:
		ModuleArmorPenetration();
		virtual ~ModuleArmorPenetration() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}