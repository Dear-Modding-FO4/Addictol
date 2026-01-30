#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleLoadScreen :
		public Module
	{
	public:
		ModuleLoadScreen();
		virtual ~ModuleLoadScreen() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}