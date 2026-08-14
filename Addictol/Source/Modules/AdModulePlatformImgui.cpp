#include <Modules/AdModulePlatformImgui.h>
#include <AdUtils.h>
#include <AdPlatformImgui.h>

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
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kGameLoaded))
			return PlatformImgui::GetSingleton()->InitSDM();
		return PlatformImgui::GetSingleton()->InitHooks();
	}

	bool ModulePlatformImgui::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePlatformImgui::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
