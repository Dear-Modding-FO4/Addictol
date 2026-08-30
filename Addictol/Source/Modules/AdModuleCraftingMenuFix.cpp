#include <Modules/AdModuleCraftingMenuFix.h>
#include <Core/AdUtils.h>

#include <RE/E/ExamineMenu.h>
#include <RE/T/TESDataHandler.h>
#include <RE/B/BGSKeyword.h>
#include <RE/Fallout.h>

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#undef ERROR

namespace Addictol
{

	namespace craftingMenuFixDetail
	{
		// The weapon/armor mod menu (ExamineMenu) rebuilds its possible mod list every time
		// you switch slots or craft something. The engine walks the full COBJ + OMOD arrays
		// for the current attach point, which gets really slow with big mod lists.
		// Instead of rewriting the whole thing, we build an attach point -> candidates map once
		// and temporarily point formArrays[kCOBJ] / [kOMOD] at the small subset while the
		// original function runs. The engine still does all of its own filtering, so the
		// result is the same, just without the pointless iteration over thousands of forms.

		static REL::Relocation<void (*)(RE::ExamineMenu*, RE::TESBoundObject*)> s_origBuildPossibleModList;

		// Internal idx computation inside BuildPossibleModList, resolved from its own
		// instruction bytes so we don't need extra address ids.
		// (rcx=[this+0x508], rdx=[this+0x518], r8=static data, r9=&buf, 5th=bool)
		using ComputeIdxFn = bool (*)(void*, void*, void*, void*, bool);
		static ComputeIdxFn s_computeIdxFn = nullptr;
		static void* s_idxStaticData = nullptr;

		// GetKeywordByIndex(type=2, idx) is what the engine uses to resolve attach points.
		// No id for it, but the pattern is unique across .text on every runtime we checked.
		using GetKeywordByIndexFn = RE::BGSKeyword* (*)(uint32_t, uint32_t);
		static GetKeywordByIndexFn s_keywordFn = nullptr;

		// Mirrors the layout of BSTArray<TESForm*> in TESDataHandler::formArrays. We don't
		// use BSTArray itself because std::atomic_ref needs the raw, trivially-copyable
		// data/size fields, which BSTArray keeps private. The layout is the same on OG/NG/AE.
		struct FormArrayEntry
		{
			RE::TESForm** data;  // +0x00
			uint32_t capacity;   // +0x08
			uint32_t pad0C;      // +0x0C
			uint32_t size;       // +0x10
			uint32_t pad14;      // +0x14
		};
		static_assert(sizeof(FormArrayEntry) == 0x18);

		struct Candidate
		{
			uint32_t idx;  // original array index, keeps ordering stable
			RE::TESForm* form;
		};

		struct CandidateList
		{
			std::vector<Candidate> cands;
			std::vector<RE::TESForm*> forms;  // materialized on demand for the swap
		};

		static std::mutex s_mutex;  // build + swap are serialized (BPML runs on worker threads too)
		static std::unordered_map<const RE::BGSKeyword*, CandidateList> s_cobjByAP;
		static std::unordered_map<const RE::BGSKeyword*, CandidateList> s_omodByAP;
		static uint32_t s_cobjTotal = 0;
		static uint32_t s_omodTotal = 0;
		static bool s_ready = false;

		// BuildPossibleModList runs on worker threads, so each thread keeps its own swap state
		// and scratch buffers. t_inSwap also breaks re-entrancy: if the original function calls
		// back into the vtable slot on the same thread, the nested call skips the swap instead
		// of deadlocking on the non-recursive s_mutex.
		static thread_local bool t_inSwap;

		struct SwapState
		{
			FormArrayEntry* cobjEntry = nullptr;
			RE::TESForm** cobjOldData = nullptr;
			uint32_t cobjOldSize = 0;
			FormArrayEntry* omodEntry = nullptr;
			RE::TESForm** omodOldData = nullptr;
			uint32_t omodOldSize = 0;
		};
		static thread_local SwapState t_swap;

		// Buffers for the union fallback (used when we can't figure out the attach point)
		static thread_local std::vector<Candidate> t_unionCobj;
		static thread_local std::vector<Candidate> t_unionOmod;
		static thread_local std::vector<RE::TESForm*> t_unionCobjForms;
		static thread_local std::vector<RE::TESForm*> t_unionOmodForms;

		// SEH helper -- on AE the slotObjects entries have been seen pointing at garbage, so
		// probe the pointer before reading through it. __try can't unwind C++ objects, so this
		// lives in its own function.
		[[nodiscard]] static bool IsPointerReadable(const void* a_ptr) noexcept
		{
			if (!a_ptr) return false;
			__try {
				volatile auto v = *static_cast<const volatile uint8_t*>(a_ptr);
				(void)v;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] static RE::BGSKeyword* GetCurrentAttachPoint(RE::ExamineMenu* a_menu)
		{
			if (!a_menu) return nullptr;
			auto& slotObjects = a_menu->slotObjects;
			auto idx = a_menu->slotObjectIndex;
			if (idx >= slotObjects.size()) return nullptr;
			auto* kw = slotObjects[idx].second;
			if (!kw) return nullptr;
			// paranoia -- on AE slotObjects entries have been seen pointing at garbage,
			// rather take the slow path than read through a bad pointer
			if (!IsPointerReadable(kw)) return nullptr;
			return const_cast<RE::BGSKeyword*>(kw);
		}

		// Runs the same idx computation BPML does before it does, so we know which
		// attach point we're dealing with before swapping the arrays. Writing the result
		// early is harmless, the engine recomputes the same value right after.
		[[nodiscard]] static bool UpdateSlotObjectIndex(RE::ExamineMenu* a_menu)
		{
			if (!s_computeIdxFn || !s_idxStaticData) return false;

			auto self = reinterpret_cast<char*>(a_menu);
			auto state = *reinterpret_cast<uint32_t*>(self + 0x510);
			auto* ptr518 = *reinterpret_cast<void**>(self + 0x518);
			auto* obj508 = *reinterpret_cast<void**>(self + 0x508);
			bool isStateA = (state & 0x8F) == 0x0A;

			// result buffer, layout is the same on all runtimes:
			// +0 field, +8 status (want &0x8F == 3), +16 idx
			struct alignas(8)
			{
				uint64_t field0{};
				uint32_t status{};
				uint32_t pad{};
				int32_t idx{};
				uint32_t pad2{};
			} buf{};

			if (!s_computeIdxFn(obj508, ptr518, s_idxStaticData, &buf, isStateA))
				return false;
			if ((buf.status & 0x8F) != 3) return false;

			// engine clamps negatives to 0 here (cmovns), do the same
			*reinterpret_cast<int32_t*>(self + 0x460) = (buf.idx >= 0) ? buf.idx : 0;
			return true;
		}

		// 8D 41 FF 83 F8 11 77 -- unique in .text on OG, NG and AE
		[[nodiscard]] static GetKeywordByIndexFn ScanGetKeywordByIndex()
		{
			static constexpr uint8_t kSig[] = { 0x8D, 0x41, 0xFF, 0x83, 0xF8, 0x11, 0x77 };

			const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
			const uint8_t* begin = text.GetPointer<const uint8_t>();
			const size_t size = text.GetSize();
			if (!begin || !size) return nullptr;

			const uint8_t* found = nullptr;
			int matches = 0;
			const uint8_t* end = begin + size;
			const uint8_t* p = begin;
			while (p < end &&
				   (p = static_cast<const uint8_t*>(std::memchr(p, kSig[0], static_cast<size_t>(end - p)))) != nullptr) {
				if (static_cast<size_t>(end - p) >= sizeof(kSig) && std::memcmp(p, kSig, sizeof(kSig)) == 0) {
					++matches;
					found = p;
					if (matches > 1) return nullptr;
				}
				++p;
			}
			return matches == 1 ? reinterpret_cast<GetKeywordByIndexFn>(reinterpret_cast<uintptr_t>(found)) : nullptr;
		}

		// Build attach point -> {COBJ, OMOD} candidate lists.
		// The predicates below are a superset of what the engine checks in its enumerators:
		// every form the engine would keep passes here too, and anything extra gets dropped
		// by the engine's own condition checks later. So swapping the arrays in is safe.
		[[nodiscard]] static bool EnsurePrefilterLocked()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) return false;
			auto* cobjEntry = reinterpret_cast<FormArrayEntry*>(
				&handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kCOBJ)]);
			auto* omodEntry = reinterpret_cast<FormArrayEntry*>(
				&handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kOMOD)]);
			if (!cobjEntry->data || !omodEntry->data) return false;

			// already built and the arrays haven't grown since
			if (s_ready && s_cobjTotal == cobjEntry->size && s_omodTotal == omodEntry->size)
				return true;

			s_cobjByAP.clear();
			s_omodByAP.clear();

			for (uint32_t i = 0; i < cobjEntry->size; ++i) {
				auto* form = cobjEntry->data[i];
				if (!form) continue;
				// skip deleted
				if (*reinterpret_cast<const uint32_t*>(
						reinterpret_cast<const char*>(form) + 0x10) & 0x20)
					continue;
				// created object must exist and be an OMOD
				auto* created = *reinterpret_cast<RE::TESForm* const*>(
					reinterpret_cast<const char*>(form) + 0x60);
				if (!created) continue;
				if (*reinterpret_cast<const uint8_t*>(
						reinterpret_cast<const char*>(created) + 0x1A) != 0x90)
					continue;
				auto apIdx = *reinterpret_cast<const uint16_t*>(
					reinterpret_cast<const char*>(created) + 0xC0);
				auto* kw = s_keywordFn(2, apIdx);
				if (!kw) continue;
				s_cobjByAP[kw].cands.push_back({ i, form });
			}
			for (uint32_t i = 0; i < omodEntry->size; ++i) {
				auto* form = omodEntry->data[i];
				if (!form) continue;
				auto apIdx = *reinterpret_cast<const uint16_t*>(
					reinterpret_cast<const char*>(form) + 0xC0);
				auto* kw = s_keywordFn(2, apIdx);
				if (!kw) continue;
				s_omodByAP[kw].cands.push_back({ i, form });
			}

			s_cobjTotal = cobjEntry->size;
			s_omodTotal = omodEntry->size;
			s_ready = true;
			return true;
		}

		[[nodiscard]] static std::vector<RE::TESForm*>* MaterializeLocked(CandidateList& a_list)
		{
			if (a_list.cands.empty()) return nullptr;
			if (a_list.forms.empty()) {
				a_list.forms.reserve(a_list.cands.size());
				for (const auto& c : a_list.cands) a_list.forms.push_back(c.form);
			}
			return &a_list.forms;
		}

		// Merge candidates of all slots (fallback path when the attach point is unknown)
		static void MergeUnionLocked(std::unordered_map<const RE::BGSKeyword*, CandidateList>& a_map,
			RE::ExamineMenu* a_menu, std::vector<Candidate>& a_out)
		{
			a_out.clear();
			for (const auto& slot : a_menu->slotObjects) {
				const RE::BGSKeyword* kw = slot.second;
				if (!kw) continue;
				auto it = a_map.find(kw);
				if (it == a_map.end()) continue;
				a_out.insert(a_out.end(), it->second.cands.begin(), it->second.cands.end());
			}
			std::sort(a_out.begin(), a_out.end(),
				[](const Candidate& a, const Candidate& b) { return a.idx < b.idx; });
			a_out.erase(std::unique(a_out.begin(), a_out.end(),
							[](const Candidate& a, const Candidate& b) { return a.idx == b.idx; }),
				a_out.end());
		}

		// Every reader in the engine reads size before data, so parking size at 0 while
		// swapping the pointer means concurrent readers only ever see the old array,
		// an empty one, or the new one -- never a mismatched pair.
		static void StoreSize(FormArrayEntry* a_entry, uint32_t a_value)
		{
			std::atomic_ref(a_entry->size).store(a_value, std::memory_order_release);
		}
		static void StoreData(FormArrayEntry* a_entry, RE::TESForm** a_value)
		{
			std::atomic_ref(a_entry->data).store(a_value, std::memory_order_release);
		}

		static void SwapEngage(SwapState& a_state, RE::TESForm** a_cobjData, uint32_t a_cobjCount,
			RE::TESForm** a_omodData, uint32_t a_omodCount)
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

		// Try to swap formArrays[kCOBJ]/[kOMOD] over to the candidate subset.
		// Callers must hold the lock. a_ap == nullptr falls back to the union of all slots.
		[[nodiscard]] static bool TrySwapLocked(RE::ExamineMenu* a_menu, const RE::BGSKeyword* a_ap)
		{
			if (!EnsurePrefilterLocked()) return false;

			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* cobjEntry = reinterpret_cast<FormArrayEntry*>(
				&handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kCOBJ)]);
			auto* omodEntry = reinterpret_cast<FormArrayEntry*>(
				&handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kOMOD)]);

			RE::TESForm** cobjData = nullptr;
			RE::TESForm** omodData = nullptr;
			uint32_t cobjCount = 0, omodCount = 0;

			if (a_ap) {
				auto ci = s_cobjByAP.find(a_ap);
				auto oi = s_omodByAP.find(a_ap);
				if (ci == s_cobjByAP.end() || oi == s_omodByAP.end()) return false;
				auto* cf = MaterializeLocked(ci->second);
				auto* of = MaterializeLocked(oi->second);
				if (!cf || !of) return false;
				cobjData = cf->data();
				cobjCount = static_cast<uint32_t>(cf->size());
				omodData = of->data();
				omodCount = static_cast<uint32_t>(of->size());
			} else {
				MergeUnionLocked(s_cobjByAP, a_menu, t_unionCobj);
				MergeUnionLocked(s_omodByAP, a_menu, t_unionOmod);
				if (t_unionCobj.empty() || t_unionOmod.empty()) return false;
				t_unionCobjForms.clear();
				t_unionCobjForms.reserve(t_unionCobj.size());
				for (const auto& c : t_unionCobj) t_unionCobjForms.push_back(c.form);
				t_unionOmodForms.clear();
				t_unionOmodForms.reserve(t_unionOmod.size());
				for (const auto& c : t_unionOmod) t_unionOmodForms.push_back(c.form);
				cobjData = t_unionCobjForms.data();
				cobjCount = static_cast<uint32_t>(t_unionCobjForms.size());
				omodData = t_unionOmodForms.data();
				omodCount = static_cast<uint32_t>(t_unionOmodForms.size());
			}

			// no point swapping if we're not actually shrinking anything
			if (cobjCount == 0 || omodCount == 0) return false;
			if (cobjCount >= cobjEntry->size || omodCount >= omodEntry->size) return false;

			t_swap.cobjEntry = cobjEntry;
			t_swap.cobjOldData = cobjEntry->data;
			t_swap.cobjOldSize = cobjEntry->size;
			t_swap.omodEntry = omodEntry;
			t_swap.omodOldData = omodEntry->data;
			t_swap.omodOldSize = omodEntry->size;

			SwapEngage(t_swap, cobjData, cobjCount, omodData, omodCount);
			return true;
		}

		// SEH wrapper so the arrays are always restored, even if BPML blows up
		static void RunSwapped(RE::ExamineMenu* a_menu, RE::TESBoundObject* a_object)
		{
			__try {
				s_origBuildPossibleModList(a_menu, a_object);
			}
			__finally {
				SwapRestore(t_swap);
			}
		}

		static void HookedBuildPossibleModList(RE::ExamineMenu* a_menu, RE::TESBoundObject* a_object)
		{
			bool swapped = false;

			// figure out the attach point first (falls back to nullptr = union)
			auto* apKeyword = UpdateSlotObjectIndex(a_menu) ? GetCurrentAttachPoint(a_menu) : nullptr;

			if (s_keywordFn && !t_inSwap) {
				std::unique_lock lock(s_mutex);
				if (TrySwapLocked(a_menu, apKeyword)) {
					t_inSwap = true;
					RunSwapped(a_menu, a_object);
					t_inSwap = false;
					swapped = true;
				}
			}

			if (!swapped) {
				s_origBuildPossibleModList(a_menu, a_object);
			}
		}

		// BuildPossibleModList entry bytes; OG and NG/AE differ in their frame setup.
		inline constexpr std::initializer_list<uint8_t> kBPMLPrologOG{
			0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x55,
			0x48, 0x8d, 0x68, 0xa1, 0x48, 0x81, 0xec, 0xf0, 0x00, 0x00, 0x00 };
		inline constexpr std::initializer_list<uint8_t> kBPMLPrologNG{
			0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x53, 0x10, 0x55,
			0x49, 0x8d, 0x6b, 0xa1, 0x48, 0x81, 0xec, 0xe0, 0x00, 0x00, 0x00 };

		static bool Install()
		{
			REL::Relocation<uintptr_t> vtbl{ RE::VTABLE::ExamineMenu[0] };
			const auto* vtblPtr = reinterpret_cast<const uintptr_t*>(vtbl.address());
			const auto bpmlAddr = vtblPtr[0x20];

			// Make sure slot 0x20 really is BuildPossibleModList before we touch anything.
			// If the address library resolved a wrong vtable (seen this happen on AE),
			// patching it would corrupt some other class instead. Bail out and do nothing.
			const bool isOG = RELEX::IsRuntimeOG();
			if (!RELEX::Validate(bpmlAddr, isOG ? kBPMLPrologOG : kBPMLPrologNG)) {
				REX::ERROR("CraftingMenuFix: BuildPossibleModList prologue mismatch, hook skipped"sv);
				return true;
			}

			// offsets of the "lea r8, static" and "call computeIdx" instructions inside BPML
			const uintptr_t leaOff = isOG ? 0x90 : 0x7D;
			const uintptr_t callOff = isOG ? 0xA6 : 0xA8;

			// lea r8, [rip+...]  (4c 8d 05 XX XX XX XX)
			{
				const auto* p = reinterpret_cast<const uint8_t*>(bpmlAddr + leaOff);
				if (p[0] == 0x4c && p[1] == 0x8d && p[2] == 0x05) {
					auto rel = *reinterpret_cast<const int32_t*>(p + 3);
					s_idxStaticData = reinterpret_cast<void*>(bpmlAddr + leaOff + 7 + rel);
				}
			}

			// call computeIdx  (e8 XX XX XX XX)
			{
				const auto* p = reinterpret_cast<const uint8_t*>(bpmlAddr + callOff);
				if (p[0] == 0xe8) {
					auto rel = *reinterpret_cast<const int32_t*>(p + 1);
					s_computeIdxFn = reinterpret_cast<ComputeIdxFn>(bpmlAddr + callOff + 5 + rel);
				}
			}

			// prefilter is worthless without this, but the hook itself still works fine
			s_keywordFn = ScanGetKeywordByIndex();
			if (!s_keywordFn) {
				REX::ERROR("CraftingMenuFix: GetKeywordByIndex not found, prefilter disabled"sv);
			}

			s_origBuildPossibleModList = RELEX::DetourVTable(vtbl.address(),
				reinterpret_cast<uintptr_t>(&HookedBuildPossibleModList), 0x20);
			REX::INFO("CraftingMenuFix: hook installed (prefilter {})"sv, s_keywordFn ? "on" : "off");
			return true;
		}
	}

	ModuleCraftingMenuFix::ModuleCraftingMenuFix() :
		Module("Crafting Menu Fix", &bFixesCraftingMenuFix)
	{}

	bool ModuleCraftingMenuFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			craftingMenuFixDetail::Install();

		return true;
	}
}
