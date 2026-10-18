// Thanks to WirelessLan: https://github.com/WirelessLan/F4SEPlugins/tree/main/CustomScaleFix

#include <Modules/AdModuleCustomScale.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	namespace customScaleDetail
	{

	}

	ModuleCustomScale::ModuleCustomScale() :
		Module("Custom Scale", &bFixesCustomScale, { F4SE::MessagingInterface::kNewGame, F4SE::MessagingInterface::kPreLoadGame })
	{}

	bool ModuleCustomScale::DoQuery() const noexcept
	{
		if (IsModDLLPresent("CustomScaleFix.dll"))
		{
			Skip("standalone 'CustomScaleFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleCustomScale::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		//ZoomData::Hooks_GetGeometryOffset();

		if (bAdditionalUseCameraOverrideScale.GetValue())
		{
			//PlayerAnimEvents::Hooks_PlayerAnimEvents();
		}

		if (bAdditionalUseCustomFurniturePosition.GetValue())
		{
			// FurniturePositions::Hooks_PlayFurnitureAnimation();
			// FurniturePositions::Hooks_InitializeChairBedQuickPosition();
		}

		return true;
	}

	bool ModuleCustomScale::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kNewGame || a_msg->type == F4SE::MessagingInterface::kPreLoadGame))
		{
			if (bAdditionalUseCustomFurniturePosition.GetValue())
			{
				// if (FurniturePositions::ShouldLoadFurnitureConfig)
				// 	FurniturePositions::LoadFurnitureConfig
			}
		}

		return true;
	}
}
