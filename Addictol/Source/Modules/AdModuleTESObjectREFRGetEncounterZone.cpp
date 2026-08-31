#include <Modules/AdModuleTESObjectREFRGetEncounterZone.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSEncounterZone.h>
#include <RE/B/BGSLocation.h>
#include <RE/B/BSTSmartPointer.h>
#include <RE/E/ExtraDataList.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESFormUtil.h>

#include <xbyak/xbyak.h>

namespace Addictol
{

	namespace tesObjectREFRGetEncounterZoneDetail
	{
		template <class T>
		struct GetEncounterZone
		{
			static T* thunk(const RE::BSTSmartPointer<RE::ExtraDataList>& a_in) noexcept
			{
				const auto& ref = *REX::ADJUST_POINTER<RE::TESObjectREFR>(&a_in, -static_cast<ptrdiff_t>(offsetof(RE::TESObjectREFR, RE::TESObjectREFR::extraList)));
				auto ptr = ref.extraList ? func(*ref.extraList) : nullptr;

				const auto addr = reinterpret_cast<uintptr_t>(ptr);
				if (!ref.IsInitialized() &&
					((addr & 0xFFFF'FFFF'0000'0000) == 0) &&
					((addr & 0x0000'0000'FFFF'FFFF) != 0))
				{
					auto id = static_cast<uint32_t>(addr);
					RE::TESForm::AddCompileIndex(id, ref.GetFile());
					ptr = RE::TESForm::GetFormByID<T>(id);
				}

				return ptr;
			}

			static inline REL::Relocation<T* (const RE::ExtraDataList&)> func;
		};
	}

	ModuleTESObjectREFRGetEncounterZone::ModuleTESObjectREFRGetEncounterZone() :
		Module("TESObjectREFRGetEncounterZone", &bFixesTESObjectREFRGetEncounterZone)
	{}

	bool ModuleTESObjectREFRGetEncounterZone::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<uintptr_t> Target{ REL::ID{ 1413642, 2202627 } };

		auto& trampoline = REL::GetTrampoline();

		RELEX::WriteSafe(Target.address() + 0xE, { 0x8D }); // mov -> lea
		tesObjectREFRGetEncounterZoneDetail::GetEncounterZone<RE::BGSEncounterZone>::func = trampoline.write_call<5>(Target.address() + 0x14, tesObjectREFRGetEncounterZoneDetail::GetEncounterZone<RE::BGSEncounterZone>::thunk);

		RELEX::WriteSafe(Target.address() + 0x92, { 0x8D }); // mov -> lea
		tesObjectREFRGetEncounterZoneDetail::GetEncounterZone<RE::BGSLocation>::func = trampoline.write_call<5>(Target.address() + 0x98, tesObjectREFRGetEncounterZoneDetail::GetEncounterZone<RE::BGSLocation>::thunk);

		return true;
	}

}