#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleViewmodelShading :
		public Module
	{
	public:
		ModuleViewmodelShading();
		virtual ~ModuleViewmodelShading() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
