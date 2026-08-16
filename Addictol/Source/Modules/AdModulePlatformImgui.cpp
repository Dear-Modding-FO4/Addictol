#include <Modules/AdModulePlatformImgui.h>
#include <AdPlatformImgui.h>
#include <AdUtils.h>

namespace Addictol
{
	ModulePlatformImgui::ModulePlatformImgui() :
		Module("Platform Imgui")
	{}

	bool ModulePlatformImgui::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePlatformImgui::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return PlatformImgui::InstallHooks();
		else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
			return PlatformImgui::InitializeWindow();
		return false;
	}

	bool ModulePlatformImgui::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePlatformImgui::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}

	bool ModulePlatformImgui::HasProcessDefender() noexcept
	{
		return true;
	}
}
