#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleDofFix :
		public Module
	{
	public:
		ModuleDofFix();
		virtual ~ModuleDofFix() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
