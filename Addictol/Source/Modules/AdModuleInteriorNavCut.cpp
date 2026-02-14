#include <Modules\AdModuleInteriorNavCut.h>
#include <AdUtils.h>

#include <RE/B/BGSEncounterZone.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESCellAttachDetachEvent.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

#include <ppl.h>
#include <windows.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesInteriorNavCut{ "Fixes"sv, "bInteriorNavCut"sv, true };
	static REX::TOML::Bool<> bAdditionalMultiThreading { "Additional"sv, "bInteriorNavCutMultiThreading"sv, true };

	// ---- RE Functions ---- //

	inline bool IsWorkshopItem(const RE::TESObjectREFR* a_refr)
	{
		using func_t = decltype(&IsWorkshopItem);
		static REL::Relocation<func_t> func{ REL::ID{ 1386903, 2194912 } };
		return func(a_refr);
	}

	class DynamicNavmesh
	{
	public:
		static DynamicNavmesh* GetSingleton()
		{
			REL::Relocation<DynamicNavmesh**> singleton{ REL::ID{ 908540, 2690147, 4797436 } };
			return *singleton;
		}

		void ForceUpdate()
		{
			using func_t = decltype(&DynamicNavmesh::ForceUpdate);
			static REL::Relocation<func_t> func{ REL::ID{ 854653, 2301428 } };
			return func(this);
		}
	};

	inline void RegisterForCellAttachDetach(RE::BSTEventSink<RE::TESCellAttachDetachEvent>* a_sink)
	{
		using func_t = decltype(&RegisterForCellAttachDetach);
		static REL::Relocation<func_t> func{ REL::ID{ 409767 } };
		return func(a_sink);
	}

	// ---- Performance Counter ---- //

	inline static bool PerfCounterFreqAcquired = false;
	inline static double PerfCounterFreq = 0.0;

	static void GetPerfCounterFreq()
	{
		if (!PerfCounterFreqAcquired)
		{
			LARGE_INTEGER li;
			if (QueryPerformanceFrequency(&li))
			{
				PerfCounterFreq = double(li.QuadPart) / 1000.0;
				PerfCounterFreqAcquired = true;
			}
			else
			{
				REX::WARN("InteriorNavCut: QueryPerformanceFrequency failed!"sv);
			}
		}
	}

	static int64_t StartPerfCounter()
	{
		GetPerfCounterFreq();
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return li.QuadPart;
	}

	static double GetPerfCounterMS(int64_t& counter)
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		auto result = (double(li.QuadPart - (counter)) / PerfCounterFreq);
		counter = li.QuadPart;
		return result;
	}

	// ---- Cell Attach / Detach ---- //

	void DoUpdateNavmesh(RE::TESObjectREFR* refr, bool attaching)
	{
		if (refr->parentCell->cellFlags.any(RE::TESObjectCELL::Flag::kInterior) &&
			IsWorkshopItem(refr))
		{
			if (auto enctZone = refr->parentCell->GetEncounterZone(); enctZone != nullptr && enctZone->IsWorkshop())
			{
				refr->UpdateDynamicNavmesh((refr->IsDeleted() || refr->IsDisabled()) ? false : attaching);
			}
		}
	}

	class CellAttachDetachListener : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
	{
	public:
		inline static std::atomic<bool> updateTaskQueued = false;

		static CellAttachDetachListener* GetSingleton()
		{
			static CellAttachDetachListener instance;
			return &instance;
		}

		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent& a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
		{
			if (a_event.refr != nullptr && a_event.refr->parentCell != nullptr)
			{
				DoUpdateNavmesh(a_event.refr.get(), a_event.isAttaching);
				if (!updateTaskQueued)
				{
					updateTaskQueued = true;
					F4SE::GetTaskInterface()->AddTask([]()
						{
						//auto perfTimer = StartPerfCounter();
						DynamicNavmesh::GetSingleton()->ForceUpdate();
						//always ends up being <1ms
						//logger::debug("Finished detach/attach update in {:.0f}ms", GetPerfCounterMS(perfTimer));
						updateTaskQueued = false;
						});
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	bool RegisterNavMeshUpdateListener()
	{
		if (!RELEX::IsRuntimeOG())
		{
			RE::TESCellAttachDetachEvent::GetEventSource()->RegisterSink(CellAttachDetachListener::GetSingleton());
		}
		else
		{
			RegisterForCellAttachDetach(CellAttachDetachListener::GetSingleton());
		}

		REX::INFO("InteriorNavCut: Registered Cell Attach / Detach Listener."sv);
		return true;
	}

	// ---- Load Game ---- //

	void HandleNavmeshUpdate(const RE::BSTTuple<const uint32_t, RE::TESForm*>& ele, RE::TESObjectCELL* playerCell)
	{
		RE::TESObjectCELL* cell = ele.second->As<RE::TESObjectCELL>();

		if (cell)
		{
			std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
			cell->spinLock.lock();
			auto& references = cell->references;

			for (uint32_t i = 0; i < references.size(); i++)
			{
				refs.push_back(references[i]);
			}
			cell->spinLock.unlock();

			for (auto& ref : refs)
			{
				if (ref != nullptr && ref->parentCell != nullptr)
				{
					DoUpdateNavmesh(ref.get(), ref->parentCell == playerCell);
				}
			}
		}
	}

	bool ForceNavMeshUpdate()
	{
		auto perfTimer = StartPerfCounter();

		auto playerCell = RE::PlayerCharacter::GetSingleton()->parentCell;
		//logger::info("Player cell is: {:08X}", playerCell->formID);

		const auto& [map, lock] = RE::TESForm::GetAllForms();
		RE::BSAutoReadLock l{ lock };

		if (bAdditionalMultiThreading.GetValue() == true)
		{
			concurrency::parallel_for_each(map->begin(), map->end(), [&](RE::BSTTuple<const uint32_t, RE::TESForm*> ele) { HandleNavmeshUpdate(ele, playerCell); });
		}
		else
		{
			for (const RE::BSTTuple<const uint32_t, RE::TESForm*>& ele : *map)
			{
				HandleNavmeshUpdate(ele, playerCell);
			}
		}

		DynamicNavmesh::GetSingleton()->ForceUpdate();

		REX::INFO("InteriorNavCut: Finished Load-Time Navmesh Updates in {:.0f}ms"sv, GetPerfCounterMS(perfTimer));
		return true;
	}

	ModuleInteriorNavCut::ModuleInteriorNavCut() :
		Module("Interior NavCut", &bFixesInteriorNavCut, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleInteriorNavCut::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleInteriorNavCut::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			REX::INFO("InteriorNavCut: Registering NavMesh Update Listener."sv);
			RegisterNavMeshUpdateListener();
		}

		return true;
	}

	bool ModuleInteriorNavCut::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		switch (a_msg->type)
		{
		case F4SE::MessagingInterface::kPostLoadGame:
			REX::INFO("InteriorNavCut: Forcing NavMesh Update."sv);
			ForceNavMeshUpdate();

			break;
		}

		return true;
	}

	bool ModuleInteriorNavCut::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}