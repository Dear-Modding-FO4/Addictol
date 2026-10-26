#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleArchiveStreamClose :
		public Module
	{
	public:
		ModuleArchiveStreamClose();
		virtual ~ModuleArchiveStreamClose() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
