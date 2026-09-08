#include <Modules/AdModuleArmorPenetration.h>
#include <Core/AdUtils.h>

#include <RE/A/ACTOR_VALUE_MODIFIER.h>
#include <RE/A/Actor.h>
#include <RE/T/TESBoundObject.h>

namespace Addictol
{
	namespace armorPenetrationDetail
	{
		RE::ActorValueInfo* armorPenetrationAVIF;

		struct UnequipObject // Actor::UnequipObject()
		{
			static void thunk(RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ObjectEquipParams* a_params)
			{
				func(a_actor, a_object, a_params);
				if (a_actor && a_object && a_object->GetFormType() == RE::ENUM_FORM_ID::kWEAP)
				{
					auto armorPenetration = a_actor->GetActorValue(*armorPenetrationAVIF);
					if (armorPenetration > 0.0f)
						a_actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *armorPenetrationAVIF, -armorPenetration);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleArmorPenetration::ModuleArmorPenetration() :
		Module("Armor Penetration", &bFixesArmorPenetration)
	{}

	bool ModuleArmorPenetration::DoQuery() const noexcept
	{
		if (IsModDLLPresent("ArmorPenetrationBugFixRD.dll"))
		{
			Skip("standalone 'ArmorPenetrationBugFixRD.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("ArmorPenetrationBugFixAE.dll"))
		{
			Skip("standalone 'ArmorPenetrationBugFixAE.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("ArmorPenetrationBugFix.dll"))
		{
			Skip("standalone 'ArmorPenetrationBugFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleArmorPenetration::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			armorPenetrationDetail::armorPenetrationAVIF = (RE::ActorValueInfo*)RE::TESForm::GetFormByID(0x97341);
			if (!armorPenetrationDetail::armorPenetrationAVIF)
			{
				Skip("Failed to get the Armor Penetration AVIF."sv);
				return false;
			}

			// TGX: I tried to use the ActorEquipManager Event but it seemed to be unreliable and gets called multiple times, so for now it has to be a hook.
			armorPenetrationDetail::UnequipObject::func = RELEX::DetourClassJump(RE::ID::Actor::UnequipObject.address(), &armorPenetrationDetail::UnequipObject::thunk);
		}

		return true;
	}
}
