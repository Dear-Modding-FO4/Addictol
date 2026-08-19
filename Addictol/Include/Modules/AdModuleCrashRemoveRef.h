#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCrashRemoveRef :
		public Module
	{
	public:
		ModuleCrashRemoveRef();
		virtual ~ModuleCrashRemoveRef() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
