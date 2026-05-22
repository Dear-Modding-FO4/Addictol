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

	static REX::TOML::Bool<> bWarningsReferenceHandleLimit{ "Warnings"sv, "bReferenceHandleLimit"sv, true };

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
		// 21 bits for handle, 5 for age, 1 for active = 26 bit test
		inline static constexpr uint32_t MAX_HANDLE_LIMIT = 1 << 21;

		static uint32_t GetCount() noexcept;
		static RE::BSPointerHandle<RE::TESObjectREFR> HkCreateHandle(RE::TESObjectREFR* a_ptr) noexcept;
		static void DebugInfo(const std::string_view& a_eventName) noexcept;

		inline static std::atomic_bool exceededShow{ false };
		inline static HandleManager* singleton{ nullptr };
		inline static decltype(HkCreateHandle)* CreateHandle_orig{ nullptr };
	};

	uint32_t HandleManager::GetCount() noexcept
	{
		auto manager = HandleManager::singleton;
		if (!manager || (manager->freeListHead == manager->freeListTail == (MAX_HANDLE_LIMIT - 1))) return 0;
		return manager ? manager->freeListHead : 0;
	}

	RE::BSPointerHandle<RE::TESObjectREFR> HandleManager::HkCreateHandle(RE::TESObjectREFR* a_ptr) noexcept
	{
		uint32_t count = HandleManager::GetCount();
		double ratio = static_cast<double>(count) / static_cast<float>(MAX_HANDLE_LIMIT);
		if (ratio >= WARNING_MIN_RATIO)
		{
			if (ratio >= 0.999f)
			{
				REX::CRITICAL("OUT OF HANDLE ARRAY ENTRIES! TERMINATE GAME FORCIBLY!"sv);
				REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), ERANGE);
			}
			else if (!HandleManager::exceededShow)
			{
				HandleManager::exceededShow = true;
				ratio *= 100.f;

				REX::WARN("HANDLE ARRAY ENTRIES ALMOST EXCEEDED {:.1f}%"sv, ratio);

				char szBuf[REX::W32::MAX_PATH]{};
				sprintf_s(szBuf, "Addictol::ReferenceHandleLimitWarning: HANDLE ARRAY ENTRIES ALMOST EXCEEDED (%.1f%%)", ratio);

				auto* consoleLog = RE::ConsoleLog::GetSingleton();
				if (consoleLog)
					consoleLog->AddString(szBuf);

				std::thread th([]{
					// 5 minutes in milliseconds
					std::chrono::milliseconds delay(5 * 60 * 1000);
					std::this_thread::sleep_for(delay);
					// reset
					HandleManager::exceededShow = false;
					});
				th.detach();
			}
		}

		assert(CreateHandle_orig);
		return CreateHandle_orig(a_ptr);
	}

	void HandleManager::DebugInfo(const std::string_view& a_eventName) noexcept
	{
		uint32_t count = HandleManager::GetCount();
		double ratio = static_cast<double>(count) / static_cast<float>(MAX_HANDLE_LIMIT);

		REX::INFO("Reference Handle Count ({}): {}. Ratio: {:.1f}%"sv, a_eventName, count, ratio * 100.f);
	}

	ModuleReferenceHandleLimitWarning::ModuleReferenceHandleLimitWarning() :
		Module("Reference Handle Limit Warning", &bWarningsReferenceHandleLimit,
			{ F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleReferenceHandleLimitWarning::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
		{
			HandleManager::singleton = reinterpret_cast<HandleManager*>(REL::VariantID(665313, 2688741, 4796005).address());

			*(uintptr_t*)&HandleManager::CreateHandle_orig = RELEX::DetourJump(
				RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle.address(),
				(uintptr_t)&HandleManager::HkCreateHandle);

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
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kPostLoadGame))
		{
			HandleManager::DebugInfo(GetMessagingInterfaceString(a_msg));

			return true;
		}

		return false;
	}

	bool ModuleReferenceHandleLimitWarning::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}