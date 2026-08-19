#include <Modules/AdModulePlatformImgui.h>
#include <Platform/AdPlatformImgui.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	ModulePlatformImgui::ModulePlatformImgui() :
		Module("Platform Imgui")
	{}

	bool ModulePlatformImgui::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return PlatformImgui::InstallHooks();
		else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
			return PlatformImgui::InitializeWindow();
		return false;
	}

	bool ModulePlatformImgui::HasProcessDefender() noexcept
	{
		return true;
	}
}
