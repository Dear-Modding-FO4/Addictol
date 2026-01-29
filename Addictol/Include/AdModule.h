#pragma once

#include <REX\REX\Singleton.h>
#include <REX\REX\TOML.h>
#include <REX\REX\LOG.h>
#include <F4SE\F4SE.h>
#include <string_view>

namespace Addictol
{
	class Module :
		public REX::TSingleton<Module>
	{
		std::string_view name{};
		const REX::TOML::Bool<>* option{ nullptr };
	public:
		Module(const char* a_name, const REX::TOML::Bool<>* a_option = nullptr);
		virtual ~Module() = default;

		[[nodiscard]] virtual std::string_view GetName() const noexcept { return name; }
		[[nodiscard]] virtual const REX::TOML::Bool<>* GetOption() const noexcept { return option; }

		[[nodiscard]] virtual bool DoQuery() const noexcept = 0;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept = 0;
	};
}