#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleImageSpaceAdapterWarning :
		public Module
	{
	public:
		ModuleImageSpaceAdapterWarning();
		virtual ~ModuleImageSpaceAdapterWarning() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}