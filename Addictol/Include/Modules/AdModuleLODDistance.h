#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleLODDistance :
		public Module
	{
	public:
		ModuleLODDistance();
		virtual ~ModuleLODDistance() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}