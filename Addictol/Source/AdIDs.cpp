#include <AdIDs.h>
#include <AdUtils.h>
#include <RE\IDs.h>

namespace RE_MERGE::ID
{
	static void UpdateIDs_OG() noexcept
	{
		RELEX::UpdateID(RE::ID::Setting::INISettingCollection::Singleton, 791183);
		RELEX::UpdateID(RE::ID::Setting::INIPrefSettingCollection::Singleton, 767844);
	}

	static void UpdateIDs_NG() noexcept
	{
		RELEX::UpdateID(RE::ID::ActiveEffect::CheckDisplacementSpellOnTarget, NG::ActiveEffect::CheckDisplacementSpellOnTarget);
		RELEX::UpdateID(RE::ID::ActorEquipManager::Singleton, NG::ActorEquipManager::Singleton);
		RELEX::UpdateID(RE::ID::BGSDefaultObject::Singleton, NG::BGSDefaultObject::Singleton);
		RELEX::UpdateID(RE::ID::BGSDynamicPersistenceManager::Singleton, NG::BGSDynamicPersistenceManager::Singleton);
		RELEX::UpdateID(RE::ID::BGSInventoryInterface::Singleton, NG::BGSInventoryInterface::Singleton);
		RELEX::UpdateID(RE::ID::BGSKeyword::TypedKeywords, NG::BGSKeyword::TypedKeywords);
		RELEX::UpdateID(RE::ID::BGSStoryEventManager::Singleton, NG::BGSStoryEventManager::Singleton);
		RELEX::UpdateID(RE::ID::BGSSynchronizedAnimationManager::Singleton, NG::BGSSynchronizedAnimationManager::Singleton);
		RELEX::UpdateID(RE::ID::BSIdleInputWatcher::Singleton, NG::BSIdleInputWatcher::Singleton);
		RELEX::UpdateID(RE::ID::BSInputDeviceManager::Singleton, NG::BSInputDeviceManager::Singleton);
		RELEX::UpdateID(RE::ID::BSInputEnableManager::Singleton, NG::BSInputEnableManager::Singleton);
		RELEX::UpdateID(RE::ID::BSScaleformManager::Singleton, NG::BSScaleformManager::Singleton);
		RELEX::UpdateID(RE::ID::BSStringT::Set, NG::BSStringT::Set);
		RELEX::UpdateID(RE::ID::BSTEvent::BSTGlobalEvent::Singleton, NG::BSTEvent::BSTGlobalEvent::Singleton);
		RELEX::UpdateID(RE::ID::Calendar::Singleton, NG::Calendar::Singleton);
		RELEX::UpdateID(RE::ID::CanDisplayNextHUDMessage::GetEventSource, NG::CanDisplayNextHUDMessage::GetEventSource);
		RELEX::UpdateID(RE::ID::ColorUpdateEvent::GetEventSource, NG::ColorUpdateEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::ConsoleLog::Singleton, NG::ConsoleLog::Singleton);
		RELEX::UpdateID(RE::ID::ControlMap::Singleton, NG::ControlMap::Singleton);
		RELEX::UpdateID(RE::ID::CurrentRadiationSourceCount::GetEventSource, NG::CurrentRadiationSourceCount::GetEventSource);
		RELEX::UpdateID(RE::ID::DoBeforeNewOrLoadCompletedEvent::GetEventSource, NG::DoBeforeNewOrLoadCompletedEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::ExteriorCellSingleton::Singleton, NG::ExteriorCellSingleton::Singleton);
		RELEX::UpdateID(RE::ID::FavoritesManager::Singleton, NG::FavoritesManager::Singleton);
		RELEX::UpdateID(RE::ID::FlatScreenModel::Singleton, NG::FlatScreenModel::Singleton);
		RELEX::UpdateID(RE::ID::GameScript::GameVM::Singleton, NG::GameScript::GameVM::Singleton);
		RELEX::UpdateID(RE::ID::GameUIModel::Singleton, NG::GameUIModel::Singleton);
		RELEX::UpdateID(RE::ID::HUDModeEvent::GetEventSource, NG::HUDModeEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::IFormFactory::Factories, NG::IFormFactory::Factories);
		RELEX::UpdateID(RE::ID::ImageSpaceEffectHDR::UsePipboyScreenMask, NG::ImageSpaceEffectHDR::UsePipboyScreenMask);
		RELEX::UpdateID(RE::ID::MemoryManager::Singleton, NG::MemoryManager::Singleton);
		RELEX::UpdateID(RE::ID::MenuControls::Singleton, NG::MenuControls::Singleton);
		RELEX::UpdateID(RE::ID::MenuCursor::Singleton, NG::MenuCursor::Singleton);
		RELEX::UpdateID(RE::ID::MenuTopicManager::Singleton, NG::MenuTopicManager::Singleton);
		RELEX::UpdateID(RE::ID::MessageMenuManager::Singleton, NG::MessageMenuManager::Singleton);
		RELEX::UpdateID(RE::ID::PerkPointIncreaseEvent::GetEventSource, NG::PerkPointIncreaseEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::PipboyDataManager::Singleton, NG::PipboyDataManager::Singleton);
		RELEX::UpdateID(RE::ID::PipboyLightEvent::GetEventSource, NG::PipboyLightEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::PipboyManager::Singleton, NG::PipboyManager::Singleton);
		RELEX::UpdateID(RE::ID::PlayerCamera::Singleton, NG::PlayerCamera::Singleton);
		RELEX::UpdateID(RE::ID::PlayerCharacter::Singleton, NG::PlayerCharacter::Singleton);
		RELEX::UpdateID(RE::ID::PlayerControls::Singleton, NG::PlayerControls::Singleton);
		RELEX::UpdateID(RE::ID::PlayerCrosshairModeEvent::GetEventSource, NG::PlayerCrosshairModeEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::PowerArmorGeometry::Singleton, NG::PowerArmorGeometry::Singleton);
		RELEX::UpdateID(RE::ID::ProcessLists::Singleton, NG::ProcessLists::Singleton);
		RELEX::UpdateID(RE::ID::Setting::GameSettingCollection::Singleton, NG::Setting::GameSettingCollection::Singleton);
		RELEX::UpdateID(RE::ID::SubtitleManager::Singleton, NG::SubtitleManager::Singleton);
		RELEX::UpdateID(RE::ID::TESAudio::ScriptedMusicState::Singleton, NG::TESAudio::ScriptedMusicState::Singleton);
		RELEX::UpdateID(RE::ID::TESDataHandler::Singleton, NG::TESDataHandler::Singleton);
		RELEX::UpdateID(RE::ID::TESForm::AllForms, NG::TESForm::AllForms);
		RELEX::UpdateID(RE::ID::TESForm::AllFormsMapLock, NG::TESForm::AllFormsMapLock);
		RELEX::UpdateID(RE::ID::TESForm::AllFormsByEditorID, NG::TESForm::AllFormsByEditorID);
		RELEX::UpdateID(RE::ID::TESForm::AllFormsEditorIDMapLock, NG::TESForm::AllFormsEditorIDMapLock);
		RELEX::UpdateID(RE::ID::TESObjectCELL::DefaultWater, NG::TESObjectCELL::DefaultWater);
		RELEX::UpdateID(RE::ID::TESWorldSpace::DefaultWater, NG::TESWorldSpace::DefaultWater);
		RELEX::UpdateID(RE::ID::UI::Singleton, NG::UI::Singleton);
		RELEX::UpdateID(RE::ID::UIAdvanceMenusFunctionCompleteEvent::GetEventSource, NG::UIAdvanceMenusFunctionCompleteEvent::GetEventSource);
		RELEX::UpdateID(RE::ID::UIMessageQueue::Singleton, NG::UIMessageQueue::Singleton);
		RELEX::UpdateID(RE::ID::UIUtils::UpdateGamepadDependentButtonCodes, NG::UIUtils::UpdateGamepadDependentButtonCodes);
		RELEX::UpdateID(RE::ID::VATS::Singleton, NG::VATS::Singleton);
		RELEX::UpdateID(RE::ID::Workshop::CurrentPlacementItemData, NG::Workshop::CurrentPlacementItemData);
		RELEX::UpdateID(RE::ID::Workshop::CurrentRow, NG::Workshop::CurrentRow);
		RELEX::UpdateID(RE::ID::Workshop::PlacementItem, NG::Workshop::PlacementItem);
		RELEX::UpdateID(Scaleform::ID::GFx::Value::GetMember, NG::Scaleform::GFx::Value::GetMember);
		RELEX::UpdateID(Scaleform::ID::SysAlloc::InitHeapEngine, NG::Scaleform::SysAlloc::InitHeapEngine);
	}

	void UpdateIDs() noexcept
	{
		if (RELEX::IsRuntimeOG())
			UpdateIDs_OG();
		else if (RELEX::IsRuntimeNG())
			UpdateIDs_NG();
	}
}