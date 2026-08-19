#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleSmallblockAllocator :
		public Module
	{
	public:
		ModuleSmallblockAllocator();
		virtual ~ModuleSmallblockAllocator() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}