// Thanks to Yuof for finding this bug in Skyrim: https://github.com/Yuof/CrashFixCompendium
#include <Modules/AdModuleFootIkRaycastCrash.h>
#include <Core/AdUtils.h>

#include <atomic>

namespace Addictol
{
	namespace footIkRaycastCrashDetail
	{
		using Resize_t = bool(void*, uint32_t);

		// BSTArray<CachedRaycastData> at DB +0x40: data, capacity, size.
		struct CastResults
		{
			void* data;
			uint32_t capacity;
			uint32_t pad;
			uint32_t size;
		};

		inline constexpr uint32_t kReserve = 1024;

		static Resize_t* s_initResize = nullptr;
		static Resize_t* s_growResize = nullptr;
		static std::atomic_flag s_warned;

		// RequestImpl hands out pointers into the cast-result array and drops its lock; foot-IK jobs keep
		// casting through them. Past capacity, resize reallocates and frees the block they point into.
		// The table starts at 50, so reserve room at construction, before any character exists; later grows
		// then stay in place. Slots are released whenever a reference drops its graph, so this bounds the
		// characters loaded at once, not a session total.
		static bool HKInitResize(void* a_array, uint32_t a_size) noexcept
		{
			s_initResize(a_array, kReserve);
			const auto result = s_initResize(a_array, a_size);

			const auto* const array = static_cast<const CastResults*>(a_array);
			if (array->capacity >= kReserve)
				REX::INFO("Foot IK Raycast Crash: reserved {} cast slots, {} in use"sv, array->capacity, array->size);
			else
				REX::WARN("Foot IK Raycast Crash: reserving {} cast slots failed, capacity is {}"sv, kReserve, array->capacity);
			return result;
		}

		// RequestImpl's grow, made when every slot is in use; runs under the DB lock.
		static bool HKGrowResize(void* a_array, uint32_t a_size) noexcept
		{
			const auto* const array = static_cast<const CastResults*>(a_array);
			if (a_size > array->capacity && !s_warned.test_and_set())
				REX::WARN("Foot IK Raycast Crash: over {} characters use foot IK at once; the table moves and can crash"sv, array->capacity);
			return s_growResize(a_array, a_size);
		}
	}

	ModuleFootIkRaycastCrash::ModuleFootIkRaycastCrash() :
		Module("Foot IK Raycast Crash", &bFixesFootIkRaycastCrash)
	{}

	bool ModuleFootIkRaycastCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace footIkRaycastCrashDetail;

		// Reserving after construction could free a block already in use.
		if (*REL::Relocation<void**>{ RE::ID::BGSFootIkRaycastInterfaceDB::Singleton })
		{
			Skip("the foot IK raycast table already exists"sv);
			return false;
		}

		const auto resize = REL::Relocation<uintptr_t>{ RE::ID::BSTArray::SetSize_CachedRaycastData }.address();
		const bool og = RELEX::IsRuntimeOG();

		// resize(50) on castResults: OG in the constructor, NG/AE in InitSDM, which inlines it.
		const auto initSite = og ? REL::Relocation<uintptr_t>{ RE::ID::BGSFootIkRaycastInterfaceDB::Ctor, REL::Offset{ 0xE6 } }.address() :
		                           REL::Relocation<uintptr_t>{ RE::ID::BGSFootIkRaycastInterfaceDB::InitSDM, REL::Offset{ 0x100 } }.address();
		const bool initMatches = og ?
			// lea rcx, [rdi+40h]; mov edx, 32h
			RELEX::Validate(initSite - 9, { 0x48, 0x8D, 0x4F, 0x40, 0xBA, 0x32, 0x00, 0x00, 0x00 }) :
			// mov edx, 32h; lea rcx, [castResults]
			RELEX::Validate(initSite - 12, { 0xBA, 0x32, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x0D });

		// RequestImpl's resize(size + 50): lea edx, [r9+32h] (OG) or [r11+32h] (NG/AE)
		const auto growSite = REL::Relocation<uintptr_t>{ RE::ID::BGSFootIkRaycastInterfaceDB::RequestImpl, REL::Offset{ 0x127, 0x186 } }.address();
		const bool growMatches = og ? RELEX::Validate(growSite - 11, { 0x41, 0x8D, 0x51, 0x32 }) :
		                              RELEX::Validate(growSite - 4, { 0x41, 0x8D, 0x53, 0x32 });

		if (!initMatches || !growMatches || !RELEX::IsCallTo(initSite, resize) || !RELEX::IsCallTo(growSite, resize))
		{
			Skip("the foot IK raycast table resize calls do not match; unsupported runtime or another plugin patched them"sv);
			return false;
		}

		s_initResize = reinterpret_cast<Resize_t*>(RELEX::DetourCall(initSite, reinterpret_cast<uintptr_t>(&HKInitResize)));
		s_growResize = reinterpret_cast<Resize_t*>(RELEX::DetourCall(growSite, reinterpret_cast<uintptr_t>(&HKGrowResize)));
		return s_initResize && s_growResize;
	}
}
