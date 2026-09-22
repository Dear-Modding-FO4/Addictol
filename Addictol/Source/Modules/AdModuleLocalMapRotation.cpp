#include <Modules/AdModuleLocalMapRotation.h>
#include <Core/AdUtils.h>

#include <RE/P/PipboyMapMenu.h>
#include <RE/P/PlayerCharacter.h>

namespace Addictol
{
	namespace localMapRotationDetail
	{
		float GetNorthRotation(RE::TESObjectCELL* a_cell) // TESObjectCELL::GetNorthRotation()
		{
			using func_t = decltype(&GetNorthRotation);
			static REL::Relocation<func_t> func{ REL::ID{ 915649, 2200402 } };
			return func(a_cell);
		}

		void GetLocalMapCameraExtents(RE::NiPoint3* a_topLeft, RE::NiPoint3* a_topRight, RE::NiPoint3* a_bottomLeft) // nsPipboy_LocalMap::GetLocalMapCameraExtents()
		{
			using func_t = decltype(&GetLocalMapCameraExtents);
			static REL::Relocation<func_t> func{ REL::ID{ 1020638, 2224090 } };
			return func(a_topLeft, a_topRight, a_bottomLeft);
		}

		struct SetPlayerMarker // PipboyMapMenu::SetPlayerMarker()
		{
			static void thunk(RE::PipboyMapMenu* a_pipboyMapMenu, float a_x, float a_y)
			{
				// Cells with a North Rotation
				RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
				if (player && player->parentCell && GetNorthRotation(player->parentCell) != 0.0f)
				{
					if (a_pipboyMapMenu->dataObj && a_pipboyMapMenu->dataObj->IsObject())
					{
						Scaleform::GFx::Value currentTab;
						a_pipboyMapMenu->dataObj->GetMember("CurrentTab", &currentTab);

						// Local Map only
						if (currentTab.IsUInt() && currentTab.GetUInt() == 1)
						{
							RE::NiPoint3 topLeft, topRight, bottomLeft;
							GetLocalMapCameraExtents(&topLeft, &topRight, &bottomLeft);

							RE::NiPoint3 horizontalAxis = topRight - topLeft;
							RE::NiPoint3 verticalAxis = bottomLeft - topLeft;

							const float dX = horizontalAxis.x + verticalAxis.x;
							const float dY = horizontalAxis.y + verticalAxis.y;

							if (dX != 0.0f && dY != 0.0f)
							{
								const float u = (a_x - topLeft.x) / dX;
								const float v = (a_y - topLeft.y) / dY;
								a_x = topLeft.x + u * horizontalAxis.x + v * verticalAxis.x;
								a_y = topLeft.y + u * horizontalAxis.y + v * verticalAxis.y;
							}
						}
					}
				}

				return func(a_pipboyMapMenu, a_x, a_y);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleLocalMapRotation::ModuleLocalMapRotation() :
		Module("Local Map Rotation", &bFixesLocalMapRotation)
	{}

	bool ModuleLocalMapRotation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return localMapRotationDetail::SetPlayerMarker::func = RELEX::DetourClassJump(REL::ID{ 39092, 2224087 }.address(), &localMapRotationDetail::SetPlayerMarker::thunk);
	}
}