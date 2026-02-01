#include <AdModule.h>

namespace Addictol
{
	Module::Module(const char* a_name, const REX::TOML::Bool<>* a_option, ModuleListenerType a_listeners) :
		name(a_name),
		option(a_option),
		listener_messages(a_listeners)
	{}

	bool Module::HasListener(std::uint32_t a_msgType) noexcept
	{
		return listener_messages[a_msgType];
	}
}