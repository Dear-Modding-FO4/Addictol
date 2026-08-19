#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleSafeExit :
		public Module
	{
	public:
		ModuleSafeExit();
		virtual ~ModuleSafeExit() = default;

		[[nodiscard]] static bool IsEnabledInConfig() noexcept;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}