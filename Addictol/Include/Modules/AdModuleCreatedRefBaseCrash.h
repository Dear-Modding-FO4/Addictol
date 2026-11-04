#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCreatedRefBaseCrash :
		public Module
	{
	public:
		ModuleCreatedRefBaseCrash();
		virtual ~ModuleCreatedRefBaseCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
