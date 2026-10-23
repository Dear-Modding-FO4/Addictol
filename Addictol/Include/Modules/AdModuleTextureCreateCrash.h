#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleTextureCreateCrash :
		public Module
	{
	public:
		ModuleTextureCreateCrash();
		virtual ~ModuleTextureCreateCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
