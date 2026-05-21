#include <Modules/AdModuleReferenceHandleLimitWarning.h>
#include <AdUtils.h>
#include <AdGameUtils.h>

#include <RE/C/ConsoleLog.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSPointerHandle.h>

namespace Addictol
{
	// the ratio at which we start warning in console log.
	// this can be set to anything, i just set it to 90% for right now
	constexpr auto WARNING_MIN_RATIO = 0.90f;

	// 21 bits for handle, 5 for age, 1 for active = 26 bit test
	static constexpr uint32_t MAX_HANDLE_LIMIT = 1 << 21;	
	// cached values
	static uint32_t lastCount = 0;
	static double lastRatio = 0.0f;

	static REX::TOML::Bool<> bWarningsReferenceHandleLimit{ "Warnings"sv, "bReferenceHandleLimit"sv, true };

	struct HandleManager :
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
	};

	ModuleReferenceHandleLimitWarning::ModuleReferenceHandleLimitWarning() :
		Module("Reference Handle Limit Warning", &bWarningsReferenceHandleLimit)
	{}

	static uint32_t GetReferenceHandleCount() noexcept
	{
		auto manager = reinterpret_cast<HandleManager*>(REL::VariantID(665313, 2688741, 4796005).address());
		if (!manager || (manager->freeListHead == manager->freeListTail == MAX_HANDLE_LIMIT - 1)) return 0;
		return manager ? manager->freeListHead : 0;
	}

	static void CheckReferenceHandleLimit(std::string_view eventName, bool cacheResults = true) noexcept
	{
		uint32_t count = GetReferenceHandleCount();
		double ratio = static_cast<double>(count) / static_cast<float>(MAX_HANDLE_LIMIT);

		if (cacheResults)
		{
			// we can cache it so we dont have to iterate again unless we need to measure it again
			lastCount = count;
			lastRatio = ratio;
		}

		REX::INFO("Reference Handle Count ({}): {}. Ratio: {:.1f}%"sv, eventName, count, ratio * 100.f);

		// warn if needed
		if (ratio >= WARNING_MIN_RATIO)
		{
			if (ratio >= 0.999f)
			{
				REX::CRITICAL("OUT OF HANDLE ARRAY ENTRIES!TERMINATE GAME FORCIBLY!"sv);
				REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), ERANGE);
			}
			else
			{
				REX::WARN("HANDLE ARRAY ENTRIES ALMOST EXCEEDED"sv);

				auto* consoleLog = RE::ConsoleLog::GetSingleton();
				if (consoleLog)
					consoleLog->AddString("Addictol::ReferenceHandleLimitWarning: HANDLE ARRAY ENTRIES ALMOST EXCEEDED");
			}
		}
	}

	bool ModuleReferenceHandleLimitWarning::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return true;
		
		std::string eventName = GetMessagingInterfaceString(a_msg);
		CheckReferenceHandleLimit(eventName);

		// FIXME: Add check to CreateHandle function
		
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}