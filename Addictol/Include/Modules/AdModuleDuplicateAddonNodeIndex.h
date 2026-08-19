#pragma once

#include <Core/AdModule.h>
#include <Core/AdGameUtils.h>

namespace Addictol
{
	class ModuleDuplicateAddonNodeIndex :
		public Module
	{
	public:
		ModuleDuplicateAddonNodeIndex();
		virtual ~ModuleDuplicateAddonNodeIndex() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}