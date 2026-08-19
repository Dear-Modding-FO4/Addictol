#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleControlSamplers :
		public Module
	{
	public:
		ModuleControlSamplers();
		virtual ~ModuleControlSamplers() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept override;
	};
}