#include <AdModule.h>

namespace Addictol
{
	Module::Module(const char* a_name, const REX::TOML::Bool<>* a_option, 
		std::initializer_list<std::uint32_t> a_listeners) :
		name(a_name),
		option(a_option)
	{
		for (auto bit : a_listeners)
			listener_messages.set(bit);
	}

	bool Module::HasListener(std::uint32_t a_msgType) noexcept
	{
		return listener_messages[a_msgType];
	}
}