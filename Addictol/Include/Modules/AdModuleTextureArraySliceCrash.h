#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleTextureArraySliceCrash :
		public Module
	{
	public:
		ModuleTextureArraySliceCrash();
		virtual ~ModuleTextureArraySliceCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
