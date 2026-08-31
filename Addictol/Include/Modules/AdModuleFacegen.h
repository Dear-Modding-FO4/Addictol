#pragma once

#include <Core/AdModule.h>
#include <Modules/AdFacegenExceptions.h>

namespace Addictol
{
	class ModuleFacegen :
		public Module
	{
	public:
		ModuleFacegen();
		virtual ~ModuleFacegen() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
	};
}