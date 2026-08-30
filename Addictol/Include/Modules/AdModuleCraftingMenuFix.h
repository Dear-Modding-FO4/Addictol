#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCraftingMenuFix :
		public Module
	{
	public:
		ModuleCraftingMenuFix();
		virtual ~ModuleCraftingMenuFix() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
