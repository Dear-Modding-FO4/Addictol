#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleScaleformAllocator :
		public Module
	{
	public:
		ModuleScaleformAllocator();
		virtual ~ModuleScaleformAllocator() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}