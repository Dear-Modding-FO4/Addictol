#pragma once

#include <string>
#include <initializer_list>
#include <stdint.h>
#include <REL\ID.h>

std::string AdGetRuntimePath() noexcept;
std::string AdGetRuntimeDirectory() noexcept;

namespace RE_MERGE::ID
{
	namespace NG
	{
		namespace ActiveEffect
		{
			inline constexpr auto CheckDisplacementSpellOnTarget{ 1415178 };
		}

		namespace ActorEquipManager
		{
			inline constexpr auto Singleton{ 2690994 };
		}

		namespace BGSDefaultObject
		{
			inline constexpr auto Singleton{ 2690473 };
		}

		namespace BGSDynamicPersistenceManager
		{
			inline constexpr auto Singleton{ 109630 };
		}

		namespace BGSInventoryInterface
		{
			inline constexpr auto Singleton{ 2689299 };
		}

		namespace BGSKeyword
		{
			inline constexpr auto TypedKeywords{ 1095775 };
		}

		namespace BGSStoryEventManager
		{
			inline constexpr auto Singleton{ 2693504 };
		}

		namespace BGSSynchronizedAnimationManager
		{
			inline constexpr auto Singleton{ 2690996 };
		}

		namespace BSIdleInputWatcher
		{
			inline constexpr auto Singleton{ 0 };
		}

		namespace BSInputDeviceManager
		{
			inline constexpr auto Singleton{ 1284221 };
		}

		namespace BSInputEnableManager
		{
			inline constexpr auto Singleton{ 0 };
		}

		namespace BSScaleformManager
		{
			inline constexpr auto Singleton{ 0 };
		}

		namespace BSStringT
		{
			inline constexpr auto Set{ 2189084 };
		}

		namespace BSTEvent
		{
			namespace BSTGlobalEvent
			{
				inline constexpr auto Singleton{ 2688814 };
			}
		}

		namespace Calendar
		{
			inline constexpr auto Singleton{ 2689092 };
		}

		namespace CanDisplayNextHUDMessage
		{
			inline constexpr auto GetEventSource{ 344866 };
		}

		namespace ColorUpdateEvent
		{
			inline constexpr auto GetEventSource{ 1226590 };
		}

		namespace ConsoleLog
		{
			inline constexpr auto Singleton{ 2690148 };
		}

		namespace ControlMap
		{
			inline constexpr auto Singleton{ 2692014 };
		}

		namespace CurrentRadiationSourceCount
		{
			inline constexpr auto GetEventSource{ 2696196 };
		}

		namespace DoBeforeNewOrLoadCompletedEvent
		{
			inline constexpr auto GetEventSource{ 787908 };
		}

		namespace ExteriorCellSingleton
		{
			inline constexpr auto Singleton{ 2689084 };
		}

		namespace FavoritesManager
		{
			inline constexpr auto Singleton{ 2694399 };
		}

		namespace FlatScreenModel
		{
			inline constexpr auto Singleton{ 847741 };
		}

		namespace GameScript
		{
			namespace GameVM
			{
				inline constexpr auto Singleton{ 2689134 };
			}
		}

		namespace GameUIModel
		{
			inline constexpr auto Singleton{ 17419 };
		}

		namespace HUDModeEvent
		{
			inline constexpr auto GetEventSource{ 683142 };
		}

		namespace IFormFactory
		{
			inline constexpr auto Factories{ 2689177 };
		}

		namespace ImageSpaceEffectHDR
		{
			inline constexpr auto UsePipboyScreenMask{ 2678029 };
		}

		namespace MemoryManager
		{
			inline constexpr auto Singleton{ 2193197 };
		}

		namespace MenuControls
		{
			inline constexpr auto Singleton{ 2689089 };
		}

		namespace MenuCursor
		{
			inline constexpr auto Singleton{ 2696546 };
		}

		namespace MenuTopicManager
		{
			inline constexpr auto Singleton{ 2689089 };
		}

		namespace MessageMenuManager
		{
			inline constexpr auto Singleton{ 2689087 };
		}

		namespace PerkPointIncreaseEvent
		{
			inline constexpr auto GetEventSource{ 2697359 };
		}

		namespace PipboyDataManager
		{
			inline constexpr auto Singleton{ 2689086 };
		}

		namespace PipboyLightEvent
		{
			inline constexpr auto GetEventSource{ 2696280 };
		}

		namespace PipboyManager
		{
			inline constexpr auto Singleton{ 2691945 };
		}

		namespace PlayerCamera
		{
			inline constexpr auto Singleton{ 2688801 };
		}

		namespace PlayerCharacter
		{
			inline constexpr auto Singleton{ 2690919 };
		}

		namespace PlayerControls
		{
			inline constexpr auto Singleton{ 2692013 };
		}

		namespace PlayerCrosshairModeEvent
		{
			inline constexpr auto GetEventSource{ 0 };
		}

		namespace PowerArmorGeometry
		{
			inline constexpr auto Singleton{ 1365745 };
		}

		namespace ProcessLists
		{
			inline constexpr auto Singleton{ 2688869 };
		}

		namespace Setting
		{
			namespace GameSettingCollection
			{
				inline constexpr auto Singleton{ 2690301 };
			}
		}

		namespace SubtitleManager
		{
			inline constexpr auto Singleton{ 0 };
		}

		namespace TESAudio
		{
			namespace ScriptedMusicState
			{
				inline constexpr auto Singleton{ 0 };
			}
		}

		namespace TESDataHandler
		{
			inline constexpr auto Singleton{ 2688883 };
		}

		namespace TESForm
		{
			inline constexpr auto AllForms{ 2689178 };
			inline constexpr auto AllFormsMapLock{ 2689189 };
			inline constexpr auto AllFormsByEditorID{ 2689179 };
			inline constexpr auto AllFormsEditorIDMapLock{ 2689190 };
		}

		namespace TESObjectCELL
		{
			inline constexpr auto DefaultWater{ 289864 };
		}

		namespace TESWorldSpace
		{
			inline constexpr auto DefaultWater{ 289864 };
		}

		namespace UI
		{
			inline constexpr auto Singleton{ 2689028 };
		}

		namespace UIAdvanceMenusFunctionCompleteEvent
		{
			inline constexpr auto GetEventSource{ 1067039 };
		}

		namespace UIMessageQueue
		{
			inline constexpr auto Singleton{ 2689091 };
		}

		namespace UIUtils
		{
			inline constexpr auto UpdateGamepadDependentButtonCodes{ 190238 };
		}

		namespace VATS
		{
			inline constexpr auto Singleton{ 2690444 };
		}

		namespace Workshop
		{
			inline constexpr auto CurrentPlacementItemData{ 1279207 };
			inline constexpr auto CurrentRow{ 833923 };
			inline constexpr auto PlacementItem{ 526727 };
		}

		namespace Scaleform
		{
			namespace GFx::Value
			{
				inline constexpr auto GetMember{ 2285936 };
			}

			namespace SysAlloc
			{
				inline constexpr auto InitHeapEngine{ 2284532 };
			}
		}
	}

	void UpdateIDs() noexcept;
}
