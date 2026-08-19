#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModulePipBoyLightInv :
		public Module
	{
	public:
		ModulePipBoyLightInv();
		virtual ~ModulePipBoyLightInv() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}