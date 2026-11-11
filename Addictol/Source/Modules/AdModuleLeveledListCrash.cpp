#include <Modules/AdModuleLeveledListCrash.h>
#include <Core/AdUtils.h>

#include <RE/L/LEVELED_OBJECT.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESLeveledList.h>

namespace Addictol
{
	namespace leveledListCrashDetail
	{
		using AddLeveledObjectFn = RE::LEVELED_OBJECT* (*)(RE::TESLeveledList*, uint16_t, uint16_t, int8_t, RE::TESForm*,
			RE::ContainerItemExtra*);
		static AddLeveledObjectFn s_originalAddLeveledObject{ nullptr };

		// Mirrors the engine: it reuses a null slot before the first higher level.
		[[nodiscard]] static bool HasReusableSlot(const RE::TESLeveledList& a_list, uint16_t a_level) noexcept
		{
			for (uint32_t index = 0; a_list.leveledLists && index < a_list.baseListCount; ++index)
			{
				const auto& entry = a_list.leveledLists[index];
				if (entry.level > a_level)
					break;
				if (!entry.form)
					return true;
			}
			return false;
		}

		// The u8 count wraps past 255, whichever path adds the entry.
		static RE::LEVELED_OBJECT* HKAddLeveledObject(RE::TESLeveledList* a_this, uint16_t a_level, uint16_t a_count,
			int8_t a_chanceNone, RE::TESForm* a_item, RE::ContainerItemExtra* a_itemExtra)
		{
			if (a_this->baseListCount == 0xFF && !HasReusableSlot(*a_this, a_level))
			{
				REX::WARN("LeveledListCrash: refused to add {:08X} to a leveled list that already holds 255 entries"sv,
					a_item ? a_item->GetFormID() : 0);
				return nullptr;
			}
			return s_originalAddLeveledObject(a_this, a_level, a_count, a_chanceNone, a_item, a_itemExtra);
		}
	}

	ModuleLeveledListCrash::ModuleLeveledListCrash() :
		Module("Leveled List Crash", &bFixesLeveledListCrash)
	{}

	bool ModuleLeveledListCrash::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GLXRM_InjectionBlocker.dll"))
		{
			Skip("standalone 'GLXRM_InjectionBlocker.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleLeveledListCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace leveledListCrashDetail;

		const auto target = RE::ID::TESLeveledList::AddLeveledObject.address();
		const auto hook = reinterpret_cast<uintptr_t>(&HKAddLeveledObject);
		const auto original = RELEX::IsRuntimeOG() ?
			RELEX::TryDetourJump(target, hook, { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x66, 0x44, 0x89, 0x44, 0x24, 0x18 }) :
			RELEX::TryDetourJump(target, hook, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x88, 0x4C, 0x24, 0x20 });
		if (!original)
		{
			Skip("AddLeveledObject prologue does not match; unsupported runtime or another plugin owns it"sv);
			return false;
		}
		s_originalAddLeveledObject = reinterpret_cast<AddLeveledObjectFn>(original);

		return true;
	}
}
