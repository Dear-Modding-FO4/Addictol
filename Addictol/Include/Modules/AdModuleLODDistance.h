#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleLODDistance :
		public Module
	{
	public:
		ModuleLODDistance();
		virtual ~ModuleLODDistance() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}