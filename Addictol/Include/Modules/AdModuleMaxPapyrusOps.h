#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleMaxPapyrusOps :
		public Module
	{
	public:
		ModuleMaxPapyrusOps();
		virtual ~ModuleMaxPapyrusOps() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}