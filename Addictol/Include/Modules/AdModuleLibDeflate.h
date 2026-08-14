#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleLibDeflate :
		public Module
	{
	public:
		ModuleLibDeflate();
		virtual ~ModuleLibDeflate() = default;

		[[nodiscard]] virtual const REX::TOML::Bool<>* GetOption() const noexcept override;
		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept override;
	};
}