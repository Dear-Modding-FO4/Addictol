#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMaxPapyrusOps :
		public Module
	{
	public:
		ModuleMaxPapyrusOps();
		virtual ~ModuleMaxPapyrusOps() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}