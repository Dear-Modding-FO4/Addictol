#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleThreads :
		public Module
	{
	public:
		ModuleThreads();
		virtual ~ModuleThreads() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}