#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleStringPoolRelease :
		public Module
	{
	public:
		ModuleStringPoolRelease();
		virtual ~ModuleStringPoolRelease() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}