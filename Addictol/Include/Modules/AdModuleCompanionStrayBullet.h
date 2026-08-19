#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleCompanionStrayBullet :
		public Module
	{
	public:
		ModuleCompanionStrayBullet();
		virtual ~ModuleCompanionStrayBullet() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
