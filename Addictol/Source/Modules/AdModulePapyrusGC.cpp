#include <Modules/AdModulePapyrusGC.h>
#include <Core/AdUtils.h>

#include <cstring>

#include <RE/B/BSScript_Array.h>
#include <RE/B/BSScript_Struct.h>
#include <RE/B/BSTArray.h>
#include <RE/B/BSTSmartPointer.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPapyrusGCBug{ "Fixes"sv, "bPapyrusGCBug"sv, true };

	namespace papyrusGCDetail
	{
		[[nodiscard]] static std::uint64_t GetTimer() noexcept
		{
			static REL::Relocation<std::uint64_t(*)()> func{ REL::ID{ 1300983, 2267999 } };
			return func();
		}

		[[nodiscard]] static float FrequencyMS() noexcept
		{
			static REL::Relocation<float*> var{ REL::ID{ 711608, 2666309 } };
			return *var;
		}

		// Nukem9's:
		// I'm explicitly defining this function because BSScript::Array and BSScript::Struct don't reimplement new[] and
		// delete[] anywhere. I don't know if this is an issue with commonlib or an issue in my code. Noinline is strictly
		// for debugging purposes.
		//
		// There's an additional performance benefit - this implementation can move trivial items very quickly.
		template<typename T>
		static void BSTArrayRemoveFastOG(RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t a_index) noexcept
		{
			using RemoveFn = void(*)(RE::BSTArray<RE::BSTSmartPointer<T>>&, std::uint32_t, std::uint32_t);

			if constexpr (std::is_same_v<T, RE::BSScript::Array>)
			{
				static REL::Relocation<RemoveFn> func{ REL::ID(430486) };
				func(a_elements, a_index, 1);
			}
			else if constexpr (std::is_same_v<T, RE::BSScript::Struct>)
			{
				static REL::Relocation<RemoveFn> func{ REL::ID(1294396) };
				func(a_elements, a_index, 1);
			}
			else
				a_elements.erase(&a_elements[a_index]);
		}

		// AE: OG's typed BSTArrayRemoveFast does not exist on NG/AE. We cannot use
		// CommonLibF4's BSTSmartPointer release path because its delete invokes a
		// trivial compiler-generated destructor that doesn't clean up engine-
		// allocated internals (BSTArray buffers, nested smart pointers, etc.).
		// Instead: steal the pointer, compact via raw memcpy, and release through
		// the engine's own typed destructor+deallocator.
		template<typename T>
		static void BSTArrayRemoveFastNG_AE(RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t a_index) noexcept
		{
			if (a_elements.empty() || a_index >= a_elements.size())
				return;

			// Steal raw pointer and nullify the smart pointer slot so TryDetach
			// (and thus CommonLibF4's delete) never fires on the doomed element.
			auto doomed = a_elements[a_index].get();
			std::memset(&a_elements[a_index], 0, sizeof(RE::BSTSmartPointer<T>));

			// Compact: relocate last element into the gap via raw copy, then
			// zero the vacated last slot. No ref counting triggered.
			const uint32_t lastIndex = a_elements.size() - 1;
			if (a_index != lastIndex)
			{
				std::memcpy(&a_elements[a_index], &a_elements[lastIndex], sizeof(RE::BSTSmartPointer<T>));
				std::memset(&a_elements[lastIndex], 0, sizeof(RE::BSTSmartPointer<T>));
			}

			a_elements.pop_back();

			// Decrement refcount from 1 to 0 (matching the engine's own pattern
			// where ProcessCleanup atomically decrements before calling release).
			(void)doomed->DecRef();

			// Release through the engine's proper destructor/deallocator.
			using ReleaseFn = void(*)(T*);
			if constexpr (std::is_same_v<T, RE::BSScript::Array>)
			{
				static REL::Relocation<ReleaseFn> func1{ REL::ID(2314909) };
				func1(doomed);

				static REL::Relocation<void(*)(T*, uint32_t)> func2{ REL::ID{ 0, 2228364, 4471524 } };
				func2(doomed, 0x30);
			}
			else if constexpr (std::is_same_v<T, RE::BSScript::Struct>)
			{
				static REL::Relocation<ReleaseFn> func{ REL::ID(2314957) };
				func(doomed);
			}
		}

		// Replaces the buggy ProcessArrayCleanup/ProcessStructCleanup loop logic.
		// Original bug: loop used index == NextIndexToClean as exit condition, which could
		// match on the very first deletion, causing premature exit after collecting one object.
		// Fix: use a counter-based approach to ensure the full time budget is utilized.
		// Credit: Nukem9 (https://github.com/Nukem9/fallout4-gc-bug-fix)
		template<typename T>
		static bool ProcessCleanup(float a_timeBudget, RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, uint32_t& a_nextIndex,
			[[maybe_unused]] void* a_unused) noexcept
		{
			bool didGC = false;

			const uint64_t startTime = GetTimer();
			const uint64_t maxEndTime = startTime + static_cast<uint64_t>(FrequencyMS() * a_timeBudget);

			uint32_t maximumElementsChecked = a_elements.size();
			uint32_t index = (a_nextIndex < a_elements.size()) ? a_nextIndex : a_elements.size() - 1;

			while (!a_elements.empty())
			{
				if (a_elements[index]->QRefCount() == 1)
				{
					didGC = true;

					if (RELEX::IsRuntimeOG())
						BSTArrayRemoveFastOG(a_elements, index);
					else
						BSTArrayRemoveFastNG_AE(a_elements, index);
				}

				if (index-- == 0)
					index = a_elements.size() - 1;

				if (a_timeBudget > 0 && GetTimer() >= maxEndTime)
					break;

				if (maximumElementsChecked-- == 1)
					break;
			}

			a_nextIndex = index;
			return didGC;
		}

		static void Install() noexcept
		{
			if (RELEX::IsRuntimeOG())
				RELEX::DetourJump(REL::Relocation(REL::ID{ 1068525 }).address(), (uintptr_t)&ProcessCleanup<RE::BSScript::Array>);
			else
			{
				auto off = REL::Relocation(REL::ID{ 2315239 }).address();

				// lea rdx, qword ptr ds : [r12 - 0x10]
				// lea r8, qword ptr ds : [r12 - 0x18]
				// xor r9d, r9d
				// movaps xmm0, xmm7
				RELEX::WriteSafe(off += 0x97, { 0x49, 0x8D, 0x54, 0x24, 0xF0, 0x4D, 0x8D, 0x44, 0x24,
					0xE8, 0x45, 0x31, 0xC9, 0x0F, 0x29, 0xF8 });
				RELEX::WriteSafeNop(off += 0x10, 0x123);
				// mov r13b, al
				// lea rbx, qword ptr ds : [r12 - 0x20]
				RELEX::WriteSafe(off + 5, { 0x41, 0x88, 0xC5, 0x49, 0x8D, 0x5C, 0x24, 0xE0 });
				RELEX::DetourCall(off, (uintptr_t)&ProcessCleanup<RE::BSScript::Array>);
			}

			RELEX::DetourJump(REL::Relocation(REL::ID{ 1466234, 2315349 }).address(), (uintptr_t)&ProcessCleanup<RE::BSScript::Struct>);
		}
	}

	ModulePapyrusGC::ModulePapyrusGC() :
		Module("PapyrusGC", &bFixesPapyrusGCBug)
	{}

	bool ModulePapyrusGC::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GCBugFix.dll"))
		{
			Skip("Standalone 'GCBugFix.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModulePapyrusGC::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoad)
			papyrusGCDetail::Install();

		return true;
	}

}
