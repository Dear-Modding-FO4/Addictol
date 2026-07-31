#include <Modules/AdModuleNegativeHealthFix.h>
#include <AdUtils.h>

#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/P/PlayerCharacter.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesNegativeHealthFix{ "Fixes"sv, "bNegativeHealthFix"sv, true };

	namespace negativeHealthFixDetail
	{
		// ActorValueOwner vtable layout (slot indices within a single vtable):
		//   00 dtor, 01 GetActorValue, 02 GetPermanentActorValue,
		//   03 GetBaseActorValue, 04 SetBaseActorValue, 05 ModBaseActorValue,
		//   06 ModActorValue, 07 GetModifier, 08 RestoreActorValue,
		//   09 SetActorValue, 0A GetIsPlayerOwner
		//
		// ActorValueOwner is a __declspec(novtable) sub-object: its base vtable
		// (RE::VTABLE::ActorValueOwner[0]) is never assigned to any instance.
		// Actual actors use the sub-object vtable inherited via TESObjectREFR,
		// which sits at index 7 in the Actor / PlayerCharacter vtable groups:
		//   0 TESForm, 1 BSHandleRefObject, 2-4 BSTEventSink x3,
		//   5 IAnimationGraphManagerHolder, 6 IKeywordFormBase, 7 ActorValueOwner
		// (BSTEventSource has no virtual functions → no vtable slot)
		using SetBaseAV_t = void(RE::ActorValueOwner*, const RE::ActorValueInfo&, float);
		using ModBaseAV_t = void(RE::ActorValueOwner*, const RE::ActorValueInfo&, float);

		static SetBaseAV_t* g_origSetBaseAV = nullptr;
		static ModBaseAV_t* g_origModBaseAV = nullptr;

		// Cache health AV pointer to avoid GetSingleton() lookup on every call
		static const RE::ActorValueInfo* g_healthAV = nullptr;

		static void HookedSetBaseActorValue(RE::ActorValueOwner* a_owner,
			const RE::ActorValueInfo& a_av, float a_value)
		{
			if (&a_av == g_healthAV && a_value < 0.0f)
				return;  // Block the write — would set health base negative
			g_origSetBaseAV(a_owner, a_av, a_value);
		}

		static void HookedModBaseActorValue(RE::ActorValueOwner* a_owner,
			const RE::ActorValueInfo& a_av, float a_delta)
		{
			if (&a_av == g_healthAV)
			{
				float currentBase = a_owner->GetBaseActorValue(a_av);
				float newBase = currentBase + a_delta;
				if (newBase < 0.0f)
					return;  // Block the write — would push health base negative
			}
			g_origModBaseAV(a_owner, a_av, a_delta);
		}

		static void InstallHooks() noexcept
		{
			auto* av = RE::ActorValue::GetSingleton();
			if (!av || !av->health)
			{
				REX::ERROR("NegativeHealthFix: failed to get health ActorValueInfo, hooks not installed"sv);
				return;
			}
			g_healthAV = av->health;

			// Hook the ActorValueOwner sub-object vtable from Actor's vtable group
			// (index 7). Actor[7] covers all Actor instances (NPCs, creatures).
			REL::Relocation<std::uintptr_t> vtblActor{ RE::VTABLE::Actor[7] };
			g_origSetBaseAV = reinterpret_cast<SetBaseAV_t*>(vtblActor.write_vfunc(4, &HookedSetBaseActorValue));
			g_origModBaseAV = reinterpret_cast<ModBaseAV_t*>(vtblActor.write_vfunc(5, &HookedModBaseActorValue));

			// PlayerCharacter has its own vtable copy — hook it separately so the
			// player is covered too. The original function pointer is the same as
			// Actor's (PlayerCharacter does not override these slots), so g_orig*
			// saved above is correct for both.
			REL::Relocation<std::uintptr_t> vtblPlayer{ RE::VTABLE::PlayerCharacter[7] };
			vtblPlayer.write_vfunc(4, &HookedSetBaseActorValue);
			vtblPlayer.write_vfunc(5, &HookedModBaseActorValue);

			REX::INFO("NegativeHealthFix: ActorValueOwner hooks installed (Actor + PlayerCharacter, SetBaseAV + ModBaseAV)"sv);
		}
	}

	ModuleNegativeHealthFix::ModuleNegativeHealthFix() :
		Module("Negative Health Fix", &bFixesNegativeHealthFix)
	{}

	bool ModuleNegativeHealthFix::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleNegativeHealthFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Install at GameDataReady stage
		if (a_msg)
		{
			if (a_msg->type != F4SE::MessagingInterface::kGameDataReady)
				return true;
		}

		negativeHealthFixDetail::InstallHooks();
		return true;
	}

	bool ModuleNegativeHealthFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleNegativeHealthFix::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
