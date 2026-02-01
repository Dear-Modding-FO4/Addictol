#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleInitTints :
		public Module
	{
	public:
		ModuleInitTints();
		virtual ~ModuleInitTints() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}