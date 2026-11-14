#include <Modules/AdModuleGrimReaperAPFix.h>
#include <Core/AdUtils.h>

#include <RE/A/ACTOR_VALUE_MODIFIER.h>
#include <RE/A/ActorValueInfo.h>

namespace Addictol
{
	namespace grimReaperAPFixDetail
	{
		std::uintptr_t returnAddress{ 0 };
		RE::ActorValueInfo* actionPoints{ nullptr };

		struct RestoreActorValue // Actor::RestoreActorValue
		{
			static void thunk(RE::ActorValueOwner* a_self, const RE::ActorValueInfo& a_info, float a_amount)
			{
				func(a_self, a_info, a_amount);
				if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) != returnAddress || &a_info != actionPoints)
					return;

				const auto damage = a_self->GetModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, a_info);
				if (damage < -0.5f)
					func(a_self, a_info, -damage);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleGrimReaperAPFix::ModuleGrimReaperAPFix() :
		Module("Grim Reaper's Sprint AP Fix", &bFixesGrimReaperAPFix)
	{}

	bool ModuleGrimReaperAPFix::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GrimReaperAPFix.dll"))
		{
			Skip("standalone 'GrimReaperAPFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleGrimReaperAPFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type != F4SE::MessagingInterface::kGameDataReady)
			return true;

		// Get the Action Points Actor Value
		const auto* actorValues = RE::ActorValue::GetSingleton();
		if (!actorValues || !actorValues->actionPoints)
		{
			Skip("actor value list is unavailable"sv);
			return false;
		}
		grimReaperAPFixDetail::actionPoints = actorValues->actionPoints;

		// Get the Return Address
		grimReaperAPFixDetail::returnAddress = REL::Relocation<uintptr_t>{ REL::ID{ 1273424, 2237217 }, REL::Offset{ 0x250, 0x254 } }.address();

		// Hook
		REL::Relocation vtable{ RE::VTABLE::PlayerCharacter[7] };
		grimReaperAPFixDetail::RestoreActorValue::func = vtable.write_vfunc(8, grimReaperAPFixDetail::RestoreActorValue::thunk);
		
		return true;
	}
}
