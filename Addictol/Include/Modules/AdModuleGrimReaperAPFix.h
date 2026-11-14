#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleGrimReaperAPFix :
		public Module
	{
	public:
		ModuleGrimReaperAPFix();
		virtual ~ModuleGrimReaperAPFix() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
