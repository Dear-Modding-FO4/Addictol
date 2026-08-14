#include <Modules/AdModuleHUDMetrics.h>
#include <AdUtils.h>

namespace Addictol
{
	ModulePlatformImgui::ModulePlatformImgui() :
		Module("HUD Metrics")
	{}

	bool ModulePlatformImgui::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePlatformImgui::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kGameLoaded))
		{
			
		}

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
