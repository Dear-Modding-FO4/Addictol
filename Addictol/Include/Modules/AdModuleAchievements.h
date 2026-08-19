#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleAchievements :
		public Module
	{
	public:
		ModuleAchievements();
		virtual ~ModuleAchievements() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}