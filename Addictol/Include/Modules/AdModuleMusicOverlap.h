#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMusicOverlap :
		public Module
	{
	public:
		ModuleMusicOverlap();
		virtual ~ModuleMusicOverlap() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
