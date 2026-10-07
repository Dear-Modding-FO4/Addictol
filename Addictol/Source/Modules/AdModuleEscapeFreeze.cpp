#include <Modules/AdModuleEscapeFreeze.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSObjectInstance.h>
#include <RE/C/ConditionCheckParams.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraInstanceData.h>
#include <RE/S/Script.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESCondition.h>
#include <RE/T/TESObjectREFR.h>

#include <vector>

namespace Addictol
{
	// A per-thread scratch replaces the global lock held across IsTrue (deadlock).
	namespace escapeFreezeDetail
	{
		constexpr REL::ID s_checkConditionFilters{ 1055546, 2206531 };

		constexpr size_t s_entryTabCount = 0x12;
		constexpr size_t s_entryConditions = 0x20;

		thread_local RE::TESObjectREFR* t_scratch{ nullptr };

		[[nodiscard]] static RE::TESObjectREFR* ThreadScratch()
		{
			if (!t_scratch)
			{
				// Engine-equivalent scratch, kept for the thread's lifetime.
				auto* scratch = RE::TESObjectREFR::Create();
				if (!scratch)
					return nullptr;

				scratch->IncRefCount();
				scratch->SetTemporary();
				t_scratch = scratch;
			}

			return t_scratch;
		}

		static void ClearScratchExtras(RE::TESObjectREFR* a_scratch)
		{
			auto* list = a_scratch->extraList.get();
			const RE::BSAutoWriteLock lock{ list->extraRWLock };
			list->extraData.RemoveAll(false);
		}

		struct CheckConditionFilters
		{
			static bool thunk(uint8_t* a_entry, uint32_t a_numArgs, RE::BGSObjectInstance** a_args, RE::ConditionCheckParams* a_params)
			{
				if (a_numArgs != a_entry[s_entryTabCount])
					return false;

				auto* scratch = ThreadScratch();
				if (!scratch)
					return func(a_entry, a_numArgs, a_args, a_params);

				auto* conditions = *reinterpret_cast<RE::TESCondition**>(a_entry + s_entryConditions);
				std::vector<RE::ExtraInstanceData> extras(a_numArgs);
				uint32_t staged = 0;
				bool result = true;

				for (uint32_t i = 0; i < a_numArgs && result; ++i)
				{
					auto* instance = a_args[i];
					auto* object = instance ? instance->object : nullptr;
					RE::TESObjectREFR* subject = nullptr;
					if (object)
					{
						if (object->IsBoundObject())
						{
							auto* bound = static_cast<RE::TESBoundObject*>(object);
							scratch->SetObjectReference(bound);

							auto& extra = extras[staged++];
							extra.data = instance->instanceData;
							extra.base = bound;

							auto* list = scratch->extraList.get();
							const RE::BSAutoWriteLock lock{ list->extraRWLock };
							list->extraData.AddExtra(&extra);
							subject = scratch;
						}
						else
						{
							subject = object->IsReference();
						}
					}

					RE::Script::ClearCachedValues();
					a_params->actionRef = subject;
					result = conditions[i].IsTrue(*a_params);
					ClearScratchExtras(scratch);
				}

				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleEscapeFreeze::ModuleEscapeFreeze() :
		Module("Escape Freeze", &bFixesEscapeFreeze)
	{}

	bool ModuleEscapeFreeze::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace escapeFreezeDetail;

		const auto target = REL::Relocation<uintptr_t>{ s_checkConditionFilters }.address();
		// mov [rsp+20h], r9; mov [rsp+8], rcx
		if (!RELEX::Validate(target, { 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08 }))
		{
			Skip("BGSEntryPointPerkEntry::CheckConditionFilters bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		CheckConditionFilters::func = RELEX::DetourClassJump(target, &CheckConditionFilters::thunk);
		if (!CheckConditionFilters::func.address())
		{
			Skip("Failed to detour BGSEntryPointPerkEntry::CheckConditionFilters."sv);
			return false;
		}

		return true;
	}
}
