#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleNavmeshLocationCrash :
		public Module
	{
	public:
		ModuleNavmeshLocationCrash();
		virtual ~ModuleNavmeshLocationCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
