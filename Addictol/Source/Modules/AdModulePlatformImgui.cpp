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
		// The load stage only exists so sinks can register before the renderer is up.
		if (!a_msg || a_msg->type != F4SE::MessagingInterface::kGameLoaded)
			return true;

		return PlatformImgui::InitializeWindow();
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
