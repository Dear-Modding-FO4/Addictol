#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleFullPrecisionDecals : public Module
	{
	public:
		ModuleFullPrecisionDecals();
		virtual ~ModuleFullPrecisionDecals() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
