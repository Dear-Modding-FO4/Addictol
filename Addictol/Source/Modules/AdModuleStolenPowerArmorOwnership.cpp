#include <Modules/AdModuleStolenPowerArmorOwnership.h>
#include <Core/AdUtils.h>
#include <Core/AdGameUtils.h>

#include <RE/T/TESFurnitureEvent.h>
#include <RE/P/PlayerCharacter.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesStolenPowerArmorOwnership{ "Fixes"sv, "bStolenPowerArmorOwnership"sv, true };

	namespace stolenPowerArmorOwnershipDetail
	{
		// Passing this command without params sets the Player as the owner
		constexpr inline static std::string_view command = "SetOwnership";

		void FixOwnership(RE::TESObjectREFR* a_powerArmorRef) noexcept
		{
			if (!a_powerArmorRef)
			{
				return;
			}

			ExecuteCommand(command, a_powerArmorRef, true);
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
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player)
					return RE::BSEventNotifyControl::kContinue;

				if (!a_event.actor.get() || a_event.actor.get() != player)
					return RE::BSEventNotifyControl::kContinue;

				RE::TESObjectREFR* furn = a_event.targetFurniture.get();
				if (!furn)
					return RE::BSEventNotifyControl::kContinue;

				// Just in case the Player hasn't used Power Armor yet
				if (!player->lastUsedPowerArmor.get() || !player->lastUsedPowerArmor.get().get())
					return RE::BSEventNotifyControl::kContinue;

				if (furn == player->lastUsedPowerArmor.get().get())
					FixOwnership(furn);

				return RE::BSEventNotifyControl::kContinue;
			}

			FurnitureEventHandler() = default;
			FurnitureEventHandler(const FurnitureEventHandler&) = delete;
			FurnitureEventHandler(FurnitureEventHandler&&) = delete;
			~FurnitureEventHandler() = default;
			FurnitureEventHandler& operator=(const FurnitureEventHandler&) = delete;
			FurnitureEventHandler& operator=(FurnitureEventHandler&&) = delete;
		};
	}

	ModuleStolenPowerArmorOwnership::ModuleStolenPowerArmorOwnership() :
		Module("Stolen Power Armor Ownership", &bFixesStolenPowerArmorOwnership, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleStolenPowerArmorOwnership::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleStolenPowerArmorOwnership::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
			RE::TESFurnitureEvent::GetEventSource()->RegisterSink(stolenPowerArmorOwnershipDetail::FurnitureEventHandler::GetSingleton());

		return true;
	}

}