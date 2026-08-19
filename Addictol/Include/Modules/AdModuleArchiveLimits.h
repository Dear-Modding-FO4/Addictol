#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleArchiveLimits :
		public Module
	{
	public:
		ModuleArchiveLimits();
		virtual ~ModuleArchiveLimits() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
	};
}