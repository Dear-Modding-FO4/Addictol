#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleRefExtended :
		public Module
	{
	public:
		ModuleRefExtended();
		virtual ~ModuleRefExtended() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}