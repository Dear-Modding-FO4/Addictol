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
		[[nodiscard]] virtual bool DoInstall() noexcept override;
	};
}