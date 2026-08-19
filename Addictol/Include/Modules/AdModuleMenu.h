#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMenu :
		public Module
	{
	public:
		ModuleMenu();
		virtual ~ModuleMenu() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
