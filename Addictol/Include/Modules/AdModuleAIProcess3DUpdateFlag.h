#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleAIProcess3DUpdateFlag :
		public Module
	{
	public:
		ModuleAIProcess3DUpdateFlag();
		virtual ~ModuleAIProcess3DUpdateFlag() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}