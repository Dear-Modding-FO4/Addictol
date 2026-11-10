#include <Modules/AdModuleLeveledListCrash.h>
#include <Core/AdUtils.h>

#include <RE/L/LEVELED_OBJECT.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESLeveledList.h>

namespace Addictol
{
	namespace leveledListCrashDetail
	{
		using AddLeveledObjectFn = RE::LEVELED_OBJECT* (*)(RE::TESLeveledList*, std::uint16_t, std::uint16_t, std::int8_t, RE::TESForm*,
			RE::ContainerItemExtra*);
		static AddLeveledObjectFn s_originalAddLeveledObject{ nullptr };

		// Mirrors the engine's scan: an empty slot within the level-sorted prefix is reused without growing.
		[[nodiscard]] static bool HasReusableSlot(const RE::TESLeveledList& a_list, std::uint16_t a_level) noexcept
		{
			for (std::uint32_t index = 0; a_list.leveledLists && index < a_list.baseListCount; ++index)
			{
				const auto& entry = a_list.leveledLists[index];
				if (entry.level > a_level)
					break;
				if (!entry.form)
					return true;
			}
			return false;
		}

		// Every add path (plugin load, save replay, Papyrus, console) lands here; the u8 count wraps past 255.
		static RE::LEVELED_OBJECT* AddLeveledObject(RE::TESLeveledList* a_this, std::uint16_t a_level, std::uint16_t a_count,
			std::int8_t a_chanceNone, RE::TESForm* a_item, RE::ContainerItemExtra* a_itemExtra)
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

		const auto original = RELEX::DetourJump(REL::Relocation{ RE::ID::TESLeveledList::AddLeveledObject },
			reinterpret_cast<uintptr_t>(&AddLeveledObject));
		if (!original)
		{
			Skip("AddLeveledObject detour installation failed"sv);
			return false;
		}
		s_originalAddLeveledObject = reinterpret_cast<AddLeveledObjectFn>(original);

		return true;
	}
}
