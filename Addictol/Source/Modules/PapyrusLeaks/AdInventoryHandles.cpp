#include <Modules/PapyrusLeaks/AdInventoryHandles.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSInventoryItem.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraUniqueID.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormDeleteEvent.h>
#include <RE/T/TESFormUtil.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESUniqueIDChangeEvent.h>

namespace Addictol::PapyrusLeaks
{
	namespace
	{
		using StackPredicate = bool (*)(RE::BGSInventoryItem::Stack&);

		void (*s_clear)(RE::BGSInventoryList*){ nullptr };
		void (*s_removeStacksIf)(RE::BGSInventoryList*, StackPredicate){ nullptr };

		// Not ~BGSInventoryList: cell unload frees refs whose item scripts must survive
		[[nodiscard]] std::array<uintptr_t, 2> ClearCallSites() noexcept
		{
			return {
				REL::Relocation{ RE::ID::TESObjectREFR::RemoveAllItems, REL::Offset{ 0x7A } }.address(),
				REL::Relocation{ RE::ID::TESObjectREFR::CopyInventoryList, REL::Offset{ 0x71, 0x76 } }.address(),
			};
		}

		[[nodiscard]] uintptr_t RemoveStacksIfEntry() noexcept
		{
			return REL::Relocation{ RE::ID::BGSInventoryList::RemoveStacksIf }.address();
		}

		// raw lookup: the container may be mid-destruction, so take no reference
		[[nodiscard]] RE::BGSInventoryList* LiveInventory(RE::TESFormID a_containerID) noexcept
		{
			const auto container = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_containerID);
			return container ? container->inventoryList : nullptr;
		}

		// Visits stacks that carry a unique ID; the caller holds a_list's lock
		template <class F>
		void ForEachUniqueID(RE::BGSInventoryList* a_list, F&& a_visit) noexcept
		{
			a_list->ForEachStack(
				[](RE::BGSInventoryItem&) { return true; },
				[&](RE::BGSInventoryItem& a_item, RE::BGSInventoryItem::Stack& a_stack) {
					const auto extra = a_stack.extra.get();
					const auto id = extra ? extra->GetByType<RE::ExtraUniqueID>() : nullptr;
					return !id || !id->uniqueID || a_visit(a_item, a_stack, *extra, *id);
				});
		}

		// Like UniqueIDUpdate: clear each dying ID, then let GameVM release its scripts
		template <class F>
		void RetireUniqueIDs(RE::BGSInventoryList* a_list, StackPredicate a_predicate, F&& a_remove) noexcept
		{
			struct Retired
			{
				uint16_t uniqueID;
				RE::TESFormID containerID;
				RE::TESFormID objectID;
			};

			std::vector<Retired> retired;
			{
				// held across a_remove, which relocks it reentrantly
				RE::BSAutoWriteLock lock{ a_list->rwLock };
				std::vector<std::pair<RE::ExtraDataList*, Retired>> dying;
				std::vector<uint16_t> kept;
				ForEachUniqueID(a_list, [&](RE::BGSInventoryItem& a_item, RE::BGSInventoryItem::Stack& a_stack, RE::ExtraDataList& a_extra, RE::ExtraUniqueID& a_id) {
					if (!a_predicate || a_predicate(a_stack))
						dying.push_back({ &a_extra, { a_id.uniqueID, a_id.baseID, a_item.object ? a_item.object->formID : 0 } });
					else
						kept.push_back(a_id.uniqueID);
					return true;
				});

				for (const auto& [extra, entry] : dying)
				{
					// barter, workshop and temp copies share other containers' extras
					if (std::ranges::find(kept, entry.uniqueID) != kept.end() || LiveInventory(entry.containerID) != a_list)
						continue;

					extra->SetUniqueID(0, 0);
					retired.push_back(entry);
				}

				a_remove();
			}

			for (const auto& entry : retired)
				RE::TESUniqueIDChangeEvent::Send(entry.uniqueID, entry.containerID, 0, 0, entry.objectID);
		}

		// RemoveAllItems without a destination, CopyInventoryList with clear
		void HKClear(RE::BGSInventoryList* a_list) noexcept
		{
			RetireUniqueIDs(a_list, nullptr, [&] { s_clear(a_list); });
		}

		// ResetInventory and template recalculation
		void HKRemoveStacksIf(RE::BGSInventoryList* a_list, StackPredicate a_predicate) noexcept
		{
			if (!a_predicate)
				return s_removeStacksIf(a_list, a_predicate);
			RetireUniqueIDs(a_list, a_predicate, [&] { s_removeStacksIf(a_list, a_predicate); });
		}

		// The VM releases only the container's own handle on deletion
		class FormDeleteHandler :
			public RE::BSTEventSink<RE::TESFormDeleteEvent>
		{
		public:
			[[nodiscard]] static FormDeleteHandler* GetSingleton() noexcept
			{
				static FormDeleteHandler singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESFormDeleteEvent& a_event, RE::BSTEventSource<RE::TESFormDeleteEvent>*) override
			{
				if (const auto list = LiveInventory(a_event.formID))
					RetireUniqueIDs(list, nullptr, [] {});
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	std::string_view InventoryHandles::CheckSites() noexcept
	{
		const auto clear = REL::Relocation{ RE::ID::BGSInventoryList::Clear }.address();
		for (const auto site : ClearCallSites())
		{
			if (!RELEX::IsCallTo(site, clear))
				return "a BGSInventoryList::Clear call site moved"sv;
		}

		// push rbx; sub rsp, 70h; lea rbx, [rcx+78h]
		if (!RELEX::Validate(RemoveStacksIfEntry(), { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8D, 0x59, 0x78 }))
			return "unexpected BGSInventoryList::RemoveStacksIf prologue"sv;

		return {};
	}

	bool InventoryHandles::Install() noexcept
	{
		for (const auto site : ClearCallSites())
			s_clear = reinterpret_cast<decltype(s_clear)>(RELEX::DetourCall(site, reinterpret_cast<uintptr_t>(&HKClear)));
		s_removeStacksIf = reinterpret_cast<decltype(s_removeStacksIf)>(RELEX::DetourJump(RemoveStacksIfEntry(), reinterpret_cast<uintptr_t>(&HKRemoveStacksIf)));
		return s_clear && s_removeStacksIf;
	}

	void InventoryHandles::RegisterSinks() noexcept
	{
		RE::TESFormDeleteEvent::GetEventSource()->RegisterSink(FormDeleteHandler::GetSingleton());
	}

	bool InventoryHandles::IsOrphan(uint64_t a_handle) noexcept
	{
		const auto containerID = static_cast<RE::TESFormID>(a_handle);
		const auto container = RE::TESForm::GetFormByID<RE::TESObjectREFR>(containerID);
		if (!container)
		{
			// unloaded refs keep their IDs reserved; deleted created ones free them
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			return (containerID >> 24) == 0xFF && dataHandler && !dataHandler->IsFormIDInuse(containerID);
		}

		const auto list = container->inventoryList;
		if (!list)
			return false;

		const auto uniqueID = static_cast<uint16_t>(a_handle >> 32);
		RE::BSAutoReadLock lock{ list->rwLock };
		bool found = false;
		ForEachUniqueID(list, [&](RE::BGSInventoryItem&, RE::BGSInventoryItem::Stack&, RE::ExtraDataList&, RE::ExtraUniqueID& a_id) {
			found = a_id.uniqueID == uniqueID;
			return !found;
		});
		return !found;
	}

	void InventoryHandles::Release(uint64_t a_handle) noexcept
	{
		RE::TESUniqueIDChangeEvent::Send(static_cast<uint16_t>(a_handle >> 32), static_cast<RE::TESFormID>(a_handle), 0, 0, 0);
	}
}
