#include <Modules/AdModuleMenu.h>
#include <Menu/AdMenu.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bAdditionalMenu{ "Additional"sv, "bMenu"sv, false };

	ModuleMenu::ModuleMenu() :
		Module("Menu", &bAdditionalMenu)
	{}

	bool ModuleMenu::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return Menu::Install();
	}

}
