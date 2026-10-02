#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleRadioSilence :
		public Module
	{
	public:
		ModuleRadioSilence();
		virtual ~ModuleRadioSilence() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}