#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleAnimSignedCrash :
		public Module
	{
	public:
		ModuleAnimSignedCrash();
		virtual ~ModuleAnimSignedCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
