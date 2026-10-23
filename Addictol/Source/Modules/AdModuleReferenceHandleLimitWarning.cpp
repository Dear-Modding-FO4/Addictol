#include <Modules/AdModuleReferenceHandleLimitWarning.h>
#include <Core/AdUtils.h>
#include <Core/AdGameUtils.h>

#include <RE/C/ConsoleLog.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSPointerHandle.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/B/BGSSaveLoadManager.h>

#define AD_NOMESSAGE_REFMANAGER 1

namespace Addictol
{
	// the ratio at which we start warning in console log.
	// this can be set to anything, i just set it to 90% for right now
	constexpr auto WARNING_MIN_RATIO = 0.90f;

	static std::atomic_bool againReport_OutOfEntries_ReferenceHandleLimit{ false };

	class HandleManager :
		public RE::BSPointerHandleManagerInterface<RE::TESObjectREFR>
	{
		struct Entries
		{
			RE::BSPointerHandle<RE::TESObjectREFR>	handle;
			void*									addressPointerInREFR;
		};

		uint32_t freeListHead;
		uint32_t freeListTail;
		RE::BSReadWriteLock handleManagerLock;
		Entries* handleEntries;
		const RE::BSPointerHandle<RE::TESObjectREFR> nullHandle{};
	public:
		// 21 bits for handle, 5 for age, 1 for active = 26 bit test (23 bits on AE with bRefExtended)
		inline static constexpr uint32_t MAX_HANDLE_LIMIT = 1 << 21;
		inline static constexpr uint32_t MAX_HANDLE_LIMIT_EXTENDED = 1 << 23;

		[[nodiscard]] static uint32_t GetMaxHandleLimit() noexcept
		{
			return (RELEX::IsRuntimeAE() && bPatchesRefExtended.GetValue()) ?
				MAX_HANDLE_LIMIT_EXTENDED :
				MAX_HANDLE_LIMIT;
		}

		static uint32_t GetCount() noexcept;

		static void DebugInfo(const std::string_view& a_eventName) noexcept;

		inline static std::atomic_bool exceededShow{ false };
		inline static HandleManager* singleton{ nullptr };

		static void OutOfEntriesMessage() noexcept;
	};

	uint32_t HandleManager::GetCount() noexcept
	{
		auto manager = HandleManager::singleton;
		if (!manager)
			return 0;
		static_assert(std::atomic_ref<uint32_t>::is_always_lock_free);
		const auto limit = GetMaxHandleLimit();
		const auto count =
			std::atomic_ref<uint32_t>(manager->freeListTail).load(std::memory_order_relaxed);
		return count != limit - 1 ? count : 0;
	}

	[[nodiscard]] static bool ReadReferenceHandleTelemetry(
		ReferenceHandleMetricSource::Values& a_values) noexcept
	{
		if (!HandleManager::singleton)
			return false;
		const auto count = HandleManager::GetCount();
		a_values = MetricDoubles(count,
			ReferenceHandleUsagePercent(count, HandleManager::GetMaxHandleLimit()));
		return true;
	}

	void HandleManager::DebugInfo(const std::string_view& a_eventName) noexcept
	{
		uint32_t count = HandleManager::GetCount();
		double ratio = static_cast<double>(count) / static_cast<float>(GetMaxHandleLimit());

		REX::INFO("Reference Handle Count ({}): {}. Ratio: {:.1f}%"sv, a_eventName, count, ratio * 100.f);
	}

	void HandleManager::OutOfEntriesMessage() noexcept
	{
		// Unfortunately, the counter is never reset, only starting the game again.
		// Saves game and quit

		auto manager = RE::BGSSaveLoadManager::GetSingleton();
		// Every failed CreateHandle lands here, possibly on several threads at once.
		if (manager && !againReport_OutOfEntries_ReferenceHandleLimit.exchange(true))
		{
			REX::ERROR("OUT OF HANDLE ARRAY ENTRIES! SAVES GAME AND QUIT FORCIBLY!");
			manager->QueueSaveLoadTask(RE::BGSSaveLoadManager::QUEUED_TASK::kSaveAndQuitToDesktop);
		}
	}

	ModuleReferenceHandleLimitWarning::ModuleReferenceHandleLimitWarning() :
		Module("Reference Handle Limit Warning", &bWarningsReferenceHandleLimit
#if !AD_NOMESSAGE_REFMANAGER
			,
			{ F4SE::MessagingInterface::kPostLoadGame }
#endif
		),
		ReferenceHandleMetricSource(kReferenceHandleMetricSchema, &ReadReferenceHandleTelemetry)
	{}

	bool ModuleReferenceHandleLimitWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
		{
			// Restore "OUT OF HANDLE ARRAY ENTRIES.  Null handle created for pointer %p." from CK.

			// Refr
			RELEX::DetourCall(REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle,
				REL::Offset{ 0xA5 }).address(), (uintptr_t)&HandleManager::OutOfEntriesMessage);
			// Actor
			RELEX::DetourCall(REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle_Actor, REL::Offset{ 0xA5, 0xC6 }).address(),
				(uintptr_t)&HandleManager::OutOfEntriesMessage);
			// Hazard
			RELEX::DetourCall(REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle_Hazard, REL::Offset{ 0xA5, 0xC6 }).address(),
				(uintptr_t)&HandleManager::OutOfEntriesMessage);
			// Projectile
			RELEX::DetourCall(REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle_Projectile, REL::Offset{ 0xA5, 0xC6 }).address(),
				(uintptr_t)&HandleManager::OutOfEntriesMessage);
			// Explosion
			RELEX::DetourCall(REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle_Explosion, REL::Offset{ 0xA5, 0xC6 }).address(),
				(uintptr_t)&HandleManager::OutOfEntriesMessage);

			// Get actual manager
			HandleManager::singleton = reinterpret_cast<HandleManager*>(REL::VariantID(665313, 2688741, 4796005).address());

			return true;
		}
		else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
		{
			HandleManager::DebugInfo(GetMessagingInterfaceString(a_msg));

			return true;
		}
		
		return false;
	}

	bool ModuleReferenceHandleLimitWarning::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
#if !AD_NOMESSAGE_REFMANAGER
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kPostLoadGame))
		{
			HandleManager::DebugInfo(GetMessagingInterfaceString(a_msg));

			return true;
		}

		return false;
#else
		return true;
#endif
	}
}