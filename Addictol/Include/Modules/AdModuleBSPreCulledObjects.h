#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleBSPreCulledObjects :
		public Module
	{
	public:
		ModuleBSPreCulledObjects();
		virtual ~ModuleBSPreCulledObjects() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}