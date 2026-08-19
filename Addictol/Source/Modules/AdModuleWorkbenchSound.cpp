#include <Modules/AdModuleWorkbenchSound.h>
#include <Core/AdUtils.h>
#include <Core/AdGameUtils.h>

#include "RE/T/TESFormUtil.h"
#include <RE/B/BGSActorCellEvent.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFurniture.h>
#include <RE/T/TESFurnitureEvent.h>
#include <RE/U/UI.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWorkbenchSound{ "Fixes"sv, "bWorkbenchSound"sv, true };

	namespace workbenchSoundDetail
	{
		static inline const std::unordered_map<RE::TESFormID, std::string_view> furnitureCommandMap =
		{
			{ 0x08674C, "RecvAnimEvent \"SoundStop\" \"NPCHumanWeldLPM\""sv },																			 // WorkshopScavengingStation
			{ 0x12EA9B, "RecvAnimEvent \"SoundStop\" \"UIWorkshopSewingMachineRunLPM\""sv },															 // WorkbenchArmorA
			{ 0x157FEB, "RecvAnimEvent \"SoundStop\" \"UIWorkshopPowerArmorWeldLPM\""sv },																 // WorkbenchPowerArmor
			{ 0x13BD08, "RecvAnimEvent \"SoundStop\" \"UIWorkshopPowerArmorWeldLPM\""sv },																 // WorkbenchPowerArmorSmall
			{ 0x17B3A4, "RecvAnimEvent \"SoundStop\" \"UIWorkshopDrillPressDrillLPM\";RecvAnimEvent \"SoundStop\" \"UIWorkshopDrillPressPowerLPM\""sv }, // workbenchWeaponsA
			{ 0x17E787, "RecvAnimEvent \"SoundStop\" \"UIWorkshopDrillPressDrillLPM\";RecvAnimEvent \"SoundStop\" \"UIWorkshopDrillPressPowerLPM\""sv }, // workbenchWeaponsB
		};

		void FixWorkbenchSounds(RE::TESObjectREFR* a_workbenchUser, RE::TESFurniture* a_furniture) noexcept
		{
			if (!a_workbenchUser)
				return;

			if (a_furniture)
			{
				// Silence the sound annotation for the given furniture
				RE::TESFormID furnitureFormID = a_furniture->GetFormID();
				if (furnitureCommandMap.contains(furnitureFormID))
					ExecuteCommand(furnitureCommandMap.at(furnitureFormID), nullptr, true);
			}
			else
			{
				// Silence all sound annotations
				for (const auto& element : furnitureCommandMap)
				{
					ExecuteCommand(element.second, a_workbenchUser, true);
				}
			}
		}

		class FurnitureEventHandler : public RE::BSTEventSink<RE::TESFurnitureEvent>
		{
		public:
			[[nodiscard]] static FurnitureEventHandler* GetSingleton()
			{
				static FurnitureEventHandler singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent& a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*)
			{
				if (!a_event.IsExit())
					// We only run on IsExit(), otherwise all workbenches would be silent
					return RE::BSEventNotifyControl::kContinue;

				RE::TESObjectREFR* furnitureRef = a_event.targetFurniture.get();
				RE::TESBoundObject* base = furnitureRef ? furnitureRef->GetObjectReference() : nullptr;
				RE::TESFurniture* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
				if (!furniture)
					return RE::BSEventNotifyControl::kContinue;

				RE::TESObjectREFR* actor = a_event.actor.get();
				if (!actor)
					return RE::BSEventNotifyControl::kContinue;

				FixWorkbenchSounds(actor, furniture);

				return RE::BSEventNotifyControl::kContinue;
			}

			FurnitureEventHandler() = default;
			FurnitureEventHandler(const FurnitureEventHandler&) = delete;
			FurnitureEventHandler(FurnitureEventHandler&&) = delete;
			~FurnitureEventHandler() = default;
			FurnitureEventHandler& operator=(const FurnitureEventHandler&) = delete;
			FurnitureEventHandler& operator=(FurnitureEventHandler&&) = delete;
		};

		class ActorCellEventHandler : public RE::BSTEventSink<RE::BGSActorCellEvent>
		{
		public:
			[[nodiscard]] static ActorCellEventHandler* GetSingleton()
			{
				static ActorCellEventHandler singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent& a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*)
			{
				RE::TESObjectREFR* actor = a_event.actor.get().get();
				if (!actor)
					return RE::BSEventNotifyControl::kContinue;

				RE::UI* ui = RE::UI::GetSingleton();
				if (!ui)
					return RE::BSEventNotifyControl::kContinue;

				if (ui->GetMenuOpen("ExamineMenu"sv) || ui->GetMenuOpen("CookingMenu"sv))
					return RE::BSEventNotifyControl::kContinue;

				FixWorkbenchSounds(actor, nullptr);

				return RE::BSEventNotifyControl::kContinue;
			}

			ActorCellEventHandler() = default;
			ActorCellEventHandler(const ActorCellEventHandler&) = delete;
			ActorCellEventHandler(ActorCellEventHandler&&) = delete;
			~ActorCellEventHandler() = default;
			ActorCellEventHandler& operator=(const ActorCellEventHandler&) = delete;
			ActorCellEventHandler& operator=(ActorCellEventHandler&&) = delete;
		};
	}

	ModuleWorkbenchSound::ModuleWorkbenchSound() :
		Module("Workbench Sound", &bFixesWorkbenchSound, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleWorkbenchSound::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleWorkbenchSound::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return false;

			RE::TESFurnitureEvent::GetEventSource()->RegisterSink(workbenchSoundDetail::FurnitureEventHandler::GetSingleton());
			player->RE::BSTEventSource<RE::BGSActorCellEvent>::RegisterSink(workbenchSoundDetail::ActorCellEventHandler::GetSingleton());

			// For existing saves
			workbenchSoundDetail::FixWorkbenchSounds(player, nullptr); 
		}

		return true;
	}

}
