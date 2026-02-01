#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModulePackageAllocateLocation :
		public Module
	{
	public:
		ModulePackageAllocateLocation();
		virtual ~ModulePackageAllocateLocation() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}