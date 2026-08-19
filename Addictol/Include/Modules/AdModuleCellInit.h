#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCellInit :
		public Module
	{
	public:
		ModuleCellInit();
		virtual ~ModuleCellInit() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}