#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleLocalMapRotation :
		public Module
	{
	public:
		ModuleLocalMapRotation();
		virtual ~ModuleLocalMapRotation() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}