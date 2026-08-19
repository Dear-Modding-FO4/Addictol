#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleUtilityShader :
		public Module
	{
	public:
		ModuleUtilityShader();
		virtual ~ModuleUtilityShader() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}