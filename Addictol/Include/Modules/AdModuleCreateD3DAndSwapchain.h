#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCreateD3DAndSwapchain :
		public Module
	{
	public:
		ModuleCreateD3DAndSwapchain();
		virtual ~ModuleCreateD3DAndSwapchain() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}