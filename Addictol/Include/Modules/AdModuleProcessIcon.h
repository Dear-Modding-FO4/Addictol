#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleProcessIcon :
		public Module
	{
	public:
		ModuleProcessIcon();
		virtual ~ModuleProcessIcon() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}