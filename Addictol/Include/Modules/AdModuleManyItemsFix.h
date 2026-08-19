#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleManyItemsFix :
		public Module
	{
	public:
		ModuleManyItemsFix();
		virtual ~ModuleManyItemsFix() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}