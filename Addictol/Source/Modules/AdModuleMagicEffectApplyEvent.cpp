#include <Modules/AdModuleMagicEffectApplyEvent.h>
#include <AdUtils.h>

#include <RE/G/GameScript.h>
#include <RE/T/TESMagicEffectApplyEvent.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMagicEffectApplyEvent{ "Fixes"sv, "bMagicEffectApplyEvent"sv, true };

	namespace magicEffectApplyDetail
	{
		struct ProcessEvent
		{
			static RE::BSEventNotifyControl thunk(RE::GameScript::CombatEventHandler& a_self, const RE::TESMagicEffectApplyEvent& a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* a_source)
			{
				return a_event.target ? func(a_self, a_event, a_source) : RE::BSEventNotifyControl::kContinue;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleMagicEffectApplyEvent::ModuleMagicEffectApplyEvent() :
		Module("MagicEffectApplyEvent", &bFixesMagicEffectApplyEvent)
	{}

	bool ModuleMagicEffectApplyEvent::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMagicEffectApplyEvent::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation Target{ REL::ID(RE::GameScript::CombatEventHandler::VTABLE[1]) };
		magicEffectApplyDetail::ProcessEvent::func = Target.write_vfunc(0x1, magicEffectApplyDetail::ProcessEvent::thunk);

		return true;
	}

	bool ModuleMagicEffectApplyEvent::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMagicEffectApplyEvent::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}