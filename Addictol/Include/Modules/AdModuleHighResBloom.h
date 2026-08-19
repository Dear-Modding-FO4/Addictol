#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleHighResBloom :
		public Module
	{
	public:
		ModuleHighResBloom();
		virtual ~ModuleHighResBloom() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
