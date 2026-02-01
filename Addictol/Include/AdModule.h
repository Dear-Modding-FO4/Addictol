#pragma once

#include <REX\REX\Singleton.h>
#include <REX\REX\TOML.h>
#include <REX\REX\LOG.h>
#include <F4SE\F4SE.h>
#include <string_view>

namespace Addictol
{
	using ModuleListenerType = std::bitset<F4SE::MessagingInterface::kGameDataReady + 1>;

	class Module :
		public REX::TSingleton<Module>
	{
		std::string_view name{};
		const REX::TOML::Bool<>* option{ nullptr };
		ModuleListenerType listener_messages;
	public:
		Module(const char* a_name, const REX::TOML::Bool<>* a_option = nullptr, ModuleListenerType a_listeners = {});
		virtual ~Module() = default;

		[[nodiscard]] virtual std::string_view GetName() const noexcept { return name; }
		[[nodiscard]] virtual const REX::TOML::Bool<>* GetOption() const noexcept { return option; }

		[[nodiscard]] virtual bool DoQuery() const noexcept = 0;
		[[nodiscard]] virtual bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept = 0;
		[[nodiscard]] virtual bool DoListener(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept = 0;
		[[nodiscard]] virtual bool HasListener(std::uint32_t a_msgType) noexcept;
	};
}