#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleHighResLocalMaps :
		public Module
	{
	public:
		ModuleHighResLocalMaps();
		virtual ~ModuleHighResLocalMaps() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
	};
}