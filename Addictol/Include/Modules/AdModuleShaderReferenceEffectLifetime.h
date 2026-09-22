#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleShaderReferenceEffectLifetime :
		public Module
	{
	public:
		ModuleShaderReferenceEffectLifetime();
		virtual ~ModuleShaderReferenceEffectLifetime() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
