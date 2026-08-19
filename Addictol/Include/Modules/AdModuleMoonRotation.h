#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMoonRotation :
		public Module
	{
	public:
		ModuleMoonRotation();
		virtual ~ModuleMoonRotation() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
