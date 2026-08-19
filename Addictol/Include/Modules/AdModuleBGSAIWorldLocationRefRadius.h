#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleBGSAIWorldLocationRefRadius :
		public Module
	{
	public:
		ModuleBGSAIWorldLocationRefRadius();
		virtual ~ModuleBGSAIWorldLocationRefRadius() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}