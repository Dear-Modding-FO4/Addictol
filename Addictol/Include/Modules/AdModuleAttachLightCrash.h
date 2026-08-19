#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleAttachLightCrash :
		public Module
	{
	public:
		ModuleAttachLightCrash();
		virtual ~ModuleAttachLightCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
