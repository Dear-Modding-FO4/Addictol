#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleSaveCompression :
		public Module
	{
	public:
		ModuleSaveCompression();
		virtual ~ModuleSaveCompression() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
