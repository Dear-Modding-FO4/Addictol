#include <Modules/AdModulePowerGridScrap.h>
#include <AdUtils.h>

#include <atomic>

// Concept ported from SUP F4SE V1170 by Tomm (MIT). Original at https://www.nexusmods.com/fallout4/mods/17295

#include <RE/B/BGSKeyword.h>
#include <RE/E/ExtraDataList.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFormUtil.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/W/Workshop.h>

namespace RE::PowerUtils
{
	inline bool operator==(const GridConnection& a_lhs, const GridConnection& a_rhs) noexcept
	{
		return a_lhs.connection == a_rhs.connection && a_lhs.connector == a_rhs.connector;
	}
}

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPowerGridScrap{ "Fixes"sv, "bPowerGridScrap"sv, true };

	using TDeleteWorkshopItem = void(__fastcall*)(RE::TESObjectREFR*);
	static REL::Relocation<TDeleteWorkshopItem> g_DeleteWorkshopItem{ REL::ID{ 853152, 2195121 } };

	using TSetWantsDelete = void(__fastcall*)(RE::TESObjectREFR*, bool);
	static TSetWantsDelete OriginalSetWantsDelete = nullptr;

	static std::atomic<RE::BGSKeyword*> g_workshopItemKeyword{ nullptr };
	static thread_local bool s_inHook = false;

	[[nodiscard]] static RE::BGSKeyword* ResolveWorkshopItemKeyword() noexcept
	{
		auto* cached = g_workshopItemKeyword.load(std::memory_order_acquire);
		if (cached)
			return cached;

		auto* keyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("WorkshopItem"sv);
		if (keyword)
			g_workshopItemKeyword.store(keyword, std::memory_order_release);
		return keyword;
	}

	[[nodiscard]] static RE::Workshop::ExtraData* GetWorkshopExtra(RE::TESObjectREFR* a_workshopRef) noexcept
	{
		if (!a_workshopRef || !a_workshopRef->extraList)
			return nullptr;
		return static_cast<RE::Workshop::ExtraData*>(
			a_workshopRef->extraList->GetByType(RE::Workshop::ExtraData::TYPE));
	}

	// V1170's IsItemPresentInWorkshop classifier: 0 = not tracked, otherwise the formID has a grid entry.
	[[nodiscard]] static bool IsItemPresentInWorkshop(RE::Workshop::ExtraData* a_extra, RE::TESFormID a_formID) noexcept
	{
		if (!a_extra)
			return false;

		for (auto* grid : a_extra->powerGrid)
		{
			if (!grid)
				continue;

			for (const auto& [key, set] : grid->adjacencyMap)
			{
				if (key == a_formID)
					return true;
				if (!set)
					continue;
				for (const auto& conn : *set)
				{
					if (conn.connection == a_formID || conn.connector == a_formID)
						return true;
				}
			}
		}
		return false;
	}

	static void __fastcall Hook_SetWantsDelete(RE::TESObjectREFR* a_this, bool a_unk)
	{
		if (s_inHook || !a_this)
		{
			if (OriginalSetWantsDelete)
				OriginalSetWantsDelete(a_this, a_unk);
			return;
		}

		auto* keyword = ResolveWorkshopItemKeyword();
		if (keyword)
		{
			if (auto* workshopRef = a_this->GetLinkedRef(keyword))
			{
				if (auto* extra = GetWorkshopExtra(workshopRef))
				{
					if (IsItemPresentInWorkshop(extra, a_this->formID))
					{
						s_inHook = true;
						g_DeleteWorkshopItem(a_this);
						s_inHook = false;
					}
				}
			}
		}

		if (OriginalSetWantsDelete)
			OriginalSetWantsDelete(a_this, a_unk);
	}

	ModulePowerGridScrap::ModulePowerGridScrap() :
		Module("Power Grid Scrap", &bFixesPowerGridScrap)
	{}

	bool ModulePowerGridScrap::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation(REL::VariantID{ 761346, 2201199 }).address();
		OriginalSetWantsDelete = reinterpret_cast<TSetWantsDelete>(
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&Hook_SetWantsDelete)));

		if (!OriginalSetWantsDelete)
		{
			REX::WARN("Power Grid Scrap: failed to detour TESObjectREFR::SetWantsDelete."sv);
			return false;
		}

		return true;
	}

}
