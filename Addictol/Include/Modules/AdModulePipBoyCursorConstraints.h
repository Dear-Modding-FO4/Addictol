#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModulePipBoyCursorConstraints :
		public Module
	{
	public:
		ModulePipBoyCursorConstraints();
		virtual ~ModulePipBoyCursorConstraints() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}