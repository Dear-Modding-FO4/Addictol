#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleCheckInternetAccess :
		public Module
	{
	public:
		ModuleCheckInternetAccess();
		virtual ~ModuleCheckInternetAccess() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}