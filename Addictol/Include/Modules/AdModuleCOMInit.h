#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleCOMInit :
		public Module
	{
	public:
		ModuleCOMInit();
		virtual ~ModuleCOMInit() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}