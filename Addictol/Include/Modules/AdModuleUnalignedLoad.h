#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleUnalignedLoad :
		public Module
	{
	public:
		ModuleUnalignedLoad();
		virtual ~ModuleUnalignedLoad() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}