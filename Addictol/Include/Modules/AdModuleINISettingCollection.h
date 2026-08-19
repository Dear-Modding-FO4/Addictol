#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleINISettingCollection :
		public Module
	{
	public:
		ModuleINISettingCollection();
		virtual ~ModuleINISettingCollection() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}