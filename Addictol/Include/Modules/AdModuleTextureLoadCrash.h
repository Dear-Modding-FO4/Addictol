#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleTextureLoadCrash :
		public Module
	{
	public:
		ModuleTextureLoadCrash();
		virtual ~ModuleTextureLoadCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}