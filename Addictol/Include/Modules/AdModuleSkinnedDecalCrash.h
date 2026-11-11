#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleSkinnedDecalCrash :
		public Module
	{
	public:
		ModuleSkinnedDecalCrash();
		virtual ~ModuleSkinnedDecalCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
