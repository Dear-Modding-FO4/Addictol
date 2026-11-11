#include <Modules/AdModuleStringPoolRelease.h>
#include <Core/AdUtils.h>

#include <RE/B/BSStringPool.h>

#include <Windows.h>

namespace Addictol
{
	namespace stringPoolReleaseDetail
	{
		using ReleaseFn = decltype(RE::BSStringPool::Entry::release)*;
		static ReleaseFn s_originalRelease{ nullptr };
		static uintptr_t s_release{ 0 };
		static uintptr_t s_stripeLock{ 0 };
		static LogLimiter s_faults{ 16, "String Pool Release: further faults will not be logged"sv };

		// Faults past Release's stripe lock must crash, not leave the lock held.
		inline constexpr REL::Offset kStripeLockOffset{ 0x87, 0x92 };

		[[nodiscard]] static int FilterFault(const EXCEPTION_POINTERS* a_info, const void* a_entry, const void* a_caller) noexcept
		{
			const auto address = reinterpret_cast<uintptr_t>(a_info->ExceptionRecord->ExceptionAddress);
			const auto beforeLock = address >= s_release && address < s_stripeLock;
			if (s_faults.Take())
			{
				const auto fault = LocateCode(a_info->ExceptionRecord->ExceptionAddress);
				const auto caller = LocateCode(a_caller);
				REX::WARN("String Pool Release: {} fault at {}+{:#x} releasing entry {:p} from {}+{:#x}"sv,
					beforeLock ? "caught" : "passed on", fault.module, fault.offset, a_entry, caller.module, caller.offset);
			}
			return beforeLock ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
		}

		// A stale entry faults in the unlocked header read; log its releaser.
		static void HKRelease(RE::BSStringPool::Entry*& a_entry) noexcept
		{
			const auto caller = _ReturnAddress();
			const void* volatile entry{ nullptr };
			__try
			{
				entry = a_entry;
				s_originalRelease(a_entry);
			}
			__except (FilterFault(GetExceptionInformation(), entry, caller))
			{}
		}
	}

	ModuleStringPoolRelease::ModuleStringPoolRelease() :
		Module("String Pool Release", &bFixesStringPoolRelease)
	{}

	bool ModuleStringPoolRelease::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace stringPoolReleaseDetail;

		s_release = RE::ID::BSStringPool::Entry::Release.address();
		s_stripeLock = REL::Relocation<uintptr_t>{ RE::ID::BSStringPool::Entry::Release, kStripeLockOffset }.address();
		if (!RELEX::Validate(s_stripeLock, { 0xF0, 0x0F, 0xB1 }))
		{
			Skip("BSStringPool Entry Release lock site does not match; unsupported runtime"sv);
			return false;
		}

		const auto original = RELEX::TryDetourJump(s_release, reinterpret_cast<uintptr_t>(&HKRelease),
			{ 0x40, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x83, 0x39, 0x00, 0x48, 0x8B, 0xF1 });
		if (!original)
		{
			Skip("BSStringPool Entry Release prologue does not match; unsupported runtime or another plugin owns it"sv);
			return false;
		}
		s_originalRelease = reinterpret_cast<ReleaseFn>(original);

		return true;
	}
}
