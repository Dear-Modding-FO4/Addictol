#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleInitTints :
		public Module
	{
	public:
		ModuleInitTints();
		virtual ~ModuleInitTints() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}