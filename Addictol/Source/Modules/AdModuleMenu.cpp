#include <Modules/AdModuleMenu.h>
#include <Menu/AdMenu.h>
#include <Core/AdUtils.h>

namespace Addictol
{

	ModuleMenu::ModuleMenu() :
		Module("Menu")
	{}

	bool ModuleMenu::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return Menu::Install();
	}

}
