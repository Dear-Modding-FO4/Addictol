#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModulePuddleCubemaps :
		public Module
	{
	public:
		ModulePuddleCubemaps();
		virtual ~ModulePuddleCubemaps() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
