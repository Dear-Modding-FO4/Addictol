#include <AdModule.h>

namespace Addictol
{
	Module::Module(const char* a_name, const REX::TOML::Bool<>* a_option) :
		name(a_name),
		option(a_option)
	{}
}