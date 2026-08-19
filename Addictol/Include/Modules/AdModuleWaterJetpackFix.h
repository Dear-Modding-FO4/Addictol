#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleWaterJetpackFix :
		public Module
	{
	public:
		ModuleWaterJetpackFix();
		virtual ~ModuleWaterJetpackFix() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}