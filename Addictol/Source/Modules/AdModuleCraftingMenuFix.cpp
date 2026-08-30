#include <Modules/AdModuleCraftingMenuFix.h>
#include <Core/AdUtils.h>

#include <RE/E/ExamineMenu.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormUtil.h>

#include <Windows.h>
#undef ERROR

namespace Addictol
{
	static REX::TOML::Bool<> bFixesCraftingMenuFix{ "Fixes"sv, "bCraftingMenuFix"sv, true };

	namespace craftingMenuFixDetail
	{
		struct Candidate
		{
			uint32_t index;
			RE::TESForm* form;
		};

		struct CandidateList
		{
			std::vector<Candidate> candidates;
			std::vector<RE::TESForm*> forms;
		};

		struct SwapState
		{
			RE::BSTArray<RE::TESForm*>* cobjEntry = nullptr;
			RE::TESForm** cobjOldData = nullptr;
			uint32_t cobjOldSize = 0;
			RE::BSTArray<RE::TESForm*>* omodEntry = nullptr;
			RE::TESForm** omodOldData = nullptr;
			uint32_t omodOldSize = 0;
		};

		static std::mutex s_mutex;
		static std::unordered_map<const RE::BGSKeyword*, CandidateList> s_cobjByAP;
		static std::unordered_map<const RE::BGSKeyword*, CandidateList> s_omodByAP;
		static uint32_t s_cobjTotal = 0;
		static uint32_t s_omodTotal = 0;
		static bool s_ready = false;
		static thread_local bool t_inSwap;
		static thread_local SwapState t_swap;

		// Buffers for the Union Fallback
		static thread_local std::vector<Candidate> t_unionCobj;
		static thread_local std::vector<Candidate> t_unionOmod;
		static thread_local std::vector<RE::TESForm*> t_unionCobjForms;
		static thread_local std::vector<RE::TESForm*> t_unionOmodForms;

		// ---- Functions ---- //

		[[nodiscard]] static RE::BGSKeyword* GetCurrentAttachPoint(RE::ExamineMenu* a_menu)
		{
			if (!a_menu || a_menu->slotObjectIndex >= a_menu->slotObjects.size())
				return nullptr;

			auto* keyword = a_menu->slotObjects[a_menu->slotObjectIndex].second;
			if (!keyword)
				return nullptr;

			return const_cast<RE::BGSKeyword*>(keyword);
		}

		[[nodiscard]] static bool UpdateSlotObjectIndex(RE::ExamineMenu* a_menu)
		{
			Scaleform::GFx::Value value;
			if (!a_menu->modSlotList.GetMember("selectedIndex"sv, &value) || !value.IsInt())
				return false;

			a_menu->slotObjectIndex = std::max(value.GetInt(), 0);
			return true;
		}

		[[nodiscard]] static bool EnsurePrefilterLocked()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler)
				return false;

			auto* cobjEntry = &handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kCOBJ)];
			auto* omodEntry = &handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kOMOD)];
			if (!cobjEntry->data() || !omodEntry->data())
				return false;

			if (s_ready && s_cobjTotal == cobjEntry->size() && s_omodTotal == omodEntry->size())
				return true;

			s_cobjByAP.clear();
			s_omodByAP.clear();

			for (uint32_t i = 0; i < cobjEntry->size(); ++i)
			{
				auto* form = cobjEntry->data()[i];
				if (!form || form->IsDeleted())
					continue;

				auto* cobj = form->As<RE::BGSConstructibleObject>();
				if (!cobj || !cobj->createdItem)
					continue;

				auto* omod = cobj->createdItem->As<RE::BGSMod::Attachment::Mod>();
				if (!omod)
					continue;

				auto* keyword = RE::BGSKeyword::GetTypedKeywordByIndex(RE::KeywordType::kAttachPoint, omod->attachPoint.keywordIndex);
				if (!keyword)
					continue;

				s_cobjByAP[keyword].candidates.push_back({ i, form });
			}

			for (uint32_t i = 0; i < omodEntry->size(); ++i)
			{
				auto* form = omodEntry->data()[i];
				if (!form)
					continue;

				auto* omod = form->As<RE::BGSMod::Attachment::Mod>();
				if (!omod)
					continue;

				auto* keyword = RE::BGSKeyword::GetTypedKeywordByIndex(RE::KeywordType::kAttachPoint, omod->attachPoint.keywordIndex);
				if (!keyword)
					continue;

				s_omodByAP[keyword].candidates.push_back({ i, form });
			}

			s_cobjTotal = cobjEntry->size();
			s_omodTotal = omodEntry->size();
			s_ready = true;

			return true;
		}

		[[nodiscard]] static std::vector<RE::TESForm*>* MaterializeLocked(CandidateList& a_list)
		{
			if (a_list.candidates.empty())
				return nullptr;

			if (a_list.forms.empty())
			{
				a_list.forms.reserve(a_list.candidates.size());
				for (const auto& c : a_list.candidates)
				{
					a_list.forms.push_back(c.form);
				}
			}

			return &a_list.forms;
		}

		static void MergeUnionLocked(std::unordered_map<const RE::BGSKeyword*, CandidateList>& a_map, RE::ExamineMenu* a_menu, std::vector<Candidate>& a_out)
		{
			a_out.clear();
			for (const auto& slot : a_menu->slotObjects)
			{
				const RE::BGSKeyword* keyword = slot.second;
				if (!keyword)
					continue;

				auto it = a_map.find(keyword);
				if (it == a_map.end())
					continue;

				a_out.insert(a_out.end(), it->second.candidates.begin(), it->second.candidates.end());
			}

			std::sort(a_out.begin(), a_out.end(), [](const Candidate& a, const Candidate& b) { return a.index < b.index; });
			a_out.erase(std::unique(a_out.begin(), a_out.end(), [](const Candidate& a, const Candidate& b) { return a.index == b.index; }), a_out.end());
		}

		// ---- Swapping ---- //

		static void StoreSize(RE::BSTArray<RE::TESForm*>* a_entry, uint32_t a_value)
		{
			std::atomic_ref(*reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(a_entry) + 0x10)).store(a_value, std::memory_order_release);
		}

		static void StoreData(RE::BSTArray<RE::TESForm*>* a_entry, RE::TESForm** a_value)
		{
			std::atomic_ref(*reinterpret_cast<RE::TESForm***>(a_entry)).store(a_value, std::memory_order_release);
		}

		static void SwapEngage(SwapState& a_state, RE::TESForm** a_cobjData, uint32_t a_cobjCount, RE::TESForm** a_omodData, uint32_t a_omodCount)
		{
			StoreSize(a_state.cobjEntry, 0);
			StoreSize(a_state.omodEntry, 0);
			StoreData(a_state.cobjEntry, a_cobjData);
			StoreData(a_state.omodEntry, a_omodData);
			StoreSize(a_state.cobjEntry, a_cobjCount);
			StoreSize(a_state.omodEntry, a_omodCount);
		}

		static void SwapRestore(SwapState& a_state)
		{
			StoreSize(a_state.cobjEntry, 0);
			StoreSize(a_state.omodEntry, 0);
			StoreData(a_state.cobjEntry, a_state.cobjOldData);
			StoreData(a_state.omodEntry, a_state.omodOldData);
			StoreSize(a_state.cobjEntry, a_state.cobjOldSize);
			StoreSize(a_state.omodEntry, a_state.omodOldSize);
		}

		[[nodiscard]] static bool TrySwapLocked(RE::ExamineMenu* a_menu, const RE::BGSKeyword* a_ap)
		{
			if (!EnsurePrefilterLocked())
				return false;

			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* cobjEntry = &handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kCOBJ)];
			auto* omodEntry = &handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kOMOD)];

			RE::TESForm** cobjData = nullptr;
			RE::TESForm** omodData = nullptr;
			uint32_t cobjCount = 0, omodCount = 0;

			if (a_ap)
			{
				auto ci = s_cobjByAP.find(a_ap);
				auto oi = s_omodByAP.find(a_ap);
				if (ci == s_cobjByAP.end() || oi == s_omodByAP.end())
					return false;

				auto* cf = MaterializeLocked(ci->second);
				auto* of = MaterializeLocked(oi->second);
				if (!cf || !of)
					return false;

				cobjData = cf->data();
				cobjCount = static_cast<uint32_t>(cf->size());
				omodData = of->data();
				omodCount = static_cast<uint32_t>(of->size());
			}
			else
			{
				MergeUnionLocked(s_cobjByAP, a_menu, t_unionCobj);
				MergeUnionLocked(s_omodByAP, a_menu, t_unionOmod);
				if (t_unionCobj.empty() || t_unionOmod.empty())
					return false;

				t_unionCobjForms.clear();
				t_unionCobjForms.reserve(t_unionCobj.size());
				for (const auto& c : t_unionCobj)
				{
					t_unionCobjForms.push_back(c.form);
				}

				t_unionOmodForms.clear();
				t_unionOmodForms.reserve(t_unionOmod.size());
				for (const auto& c : t_unionOmod)
				{
					t_unionOmodForms.push_back(c.form);
				}

				cobjData = t_unionCobjForms.data();
				cobjCount = static_cast<uint32_t>(t_unionCobjForms.size());
				omodData = t_unionOmodForms.data();
				omodCount = static_cast<uint32_t>(t_unionOmodForms.size());
			}

			if (cobjCount == 0 || omodCount == 0)
				return false;

			if (cobjCount >= cobjEntry->size() || omodCount >= omodEntry->size())
				return false;

			t_swap.cobjEntry = cobjEntry;
			t_swap.cobjOldData = cobjEntry->data();
			t_swap.cobjOldSize = cobjEntry->size();
			t_swap.omodEntry = omodEntry;
			t_swap.omodOldData = omodEntry->data();
			t_swap.omodOldSize = omodEntry->size();

			SwapEngage(t_swap, cobjData, cobjCount, omodData, omodCount);
			return true;
		}

		// ---- Hook ---- //

		struct BuildPossibleModList // ExamineMenu::BuildPossibleModList()
		{
			static void RunSwapped(RE::ExamineMenu* a_examineMenu, RE::TESBoundObject* a_tesBoundObject)
			{
				__try
				{
					func(a_examineMenu, a_tesBoundObject);
				}
				__finally
				{
					SwapRestore(t_swap);
				}
			}

			static void thunk(RE::ExamineMenu* a_examineMenu, RE::TESBoundObject* a_tesBoundObject)
			{
				bool swapped = false;
				auto* apKeyword = UpdateSlotObjectIndex(a_examineMenu) ? GetCurrentAttachPoint(a_examineMenu) : nullptr;

				if (!t_inSwap)
				{
					std::unique_lock lock(s_mutex);
					if (TrySwapLocked(a_examineMenu, apKeyword))
					{
						t_inSwap = true;
						RunSwapped(a_examineMenu, a_tesBoundObject);
						t_inSwap = false;
						swapped = true;
					}
				}

				if (!swapped)
					return func(a_examineMenu, a_tesBoundObject);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleCraftingMenuFix::ModuleCraftingMenuFix() :
		Module("Crafting Menu Fix", &bFixesCraftingMenuFix)
	{}

	bool ModuleCraftingMenuFix::DoQuery() const noexcept
	{
		if (IsModDLLPresent("CraftingMenuFix.dll"))
		{
			Skip("standalone 'CraftingMenuFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleCraftingMenuFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			REL::Relocation vtable{ RE::VTABLE::ExamineMenu[0] };
			craftingMenuFixDetail::BuildPossibleModList::func = vtable.write_vfunc(32, craftingMenuFixDetail::BuildPossibleModList::thunk);
		}

		return true;
	}
}
