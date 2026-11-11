#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCubemapArray :
		public Module
	{
	public:
		ModuleCubemapArray();
		virtual ~ModuleCubemapArray() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
