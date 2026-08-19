#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleProfile :
		public Module
	{
	public:
		ModuleProfile();
		virtual ~ModuleProfile() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}