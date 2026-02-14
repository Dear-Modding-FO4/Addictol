#include <Modules\AdModuleThreads.h>
#include <AdUtils.h>
#include <algorithm>
#include <windows.h>
#include <limits.h>
#include <comdef.h>

#undef ERROR

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesThreads{ "Patches"sv, "bThreads"sv, true };

	[[nodiscard]] static BOOL WINAPI HKSetThreadPriority(HANDLE Thread, int Priority) noexcept
	{
		// Don't allow a priority below normal - Fallout 4 doesn't have many "idle" threads
		return SetThreadPriority(Thread, std::max(THREAD_PRIORITY_NORMAL, Priority));
	}

	[[nodiscard]] static DWORD_PTR WINAPI HKSetThreadAffinityMask(HANDLE Thread, DWORD_PTR AffinityMask) noexcept
	{
		// Don't change anything
		return std::numeric_limits<DWORD_PTR>::max();
	}

	ModuleThreads::ModuleThreads() :
		Module("Threads", &bPatchesThreads)
	{}

	bool ModuleThreads::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleThreads::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto Handle = GetModuleHandleA(NULL);
		
		REL::PatchIAT((uintptr_t)&HKSetThreadPriority, "kernel32.dll", "SetThreadPriority");
		REL::PatchIAT((uintptr_t)&HKSetThreadAffinityMask, "kernel32.dll", "SetThreadAffinityMask");

		auto ProcessHandle = GetCurrentProcess();
		if (!SetPriorityClass(ProcessHandle, HIGH_PRIORITY_CLASS))
		{
			auto ErrorLast = GetLastError();
			REX::ERROR("SetPriorityClass returned failed (0x{:x}): {}"sv, ErrorLast, _com_error(ErrorLast).ErrorMessage());
			return false;
		}
		else
			REX::INFO("Set high priority has been set for process"sv);

		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
		{
			auto ErrorLast = GetLastError();
			REX::ERROR("SetThreadPriority returned failed (0x{:x}): {}"sv, ErrorLast, _com_error(ErrorLast).ErrorMessage());
			return false;
		}
		else
			REX::INFO("Set high priority has been set for main thread"sv);

		DWORD_PTR processAffinityMask, systemAffinityMask;
		if (!GetProcessAffinityMask(ProcessHandle, &processAffinityMask, &systemAffinityMask))
		{
			auto ErrorLast = GetLastError();
			REX::ERROR("GetProcessAffinityMask returned failed (0x{:x}): {}"sv, ErrorLast, _com_error(ErrorLast).ErrorMessage());
			return false;
		}
		else
		{
			REX::INFO("processAffinityMask: 0x{:X}"sv, processAffinityMask);
			REX::INFO("systemAffinityMask: 0x{:X}"sv, systemAffinityMask);

			if (processAffinityMask != systemAffinityMask)
			{
				REX::INFO("A change in the usage of processor cores has been detected"sv);

				if (!SetProcessAffinityMask(ProcessHandle, systemAffinityMask))
				{
					auto ErrorLast = GetLastError();
					REX::ERROR("SetProcessAffinityMask returned failed (0x{:x}): {}"sv, ErrorLast, _com_error(ErrorLast).ErrorMessage());
					return false;
				}
				else
					REX::INFO("Restore usage of processor cores"sv);
			}

			// Complete removal of WinAPI functions SetPriorityClass and SetProcessAffinityMask.
			// Protection against premeditated, foolishly committed spoilage of the process.

			auto kernel_32 = GetModuleHandleA("kernel32.dll");
			if (kernel_32)
			{
				auto SetPriorityClass_addr = GetProcAddress(kernel_32, "SetPriorityClass");
				auto SetProcessAffinityMask_addr = GetProcAddress(kernel_32, "SetProcessAffinityMask");
				if (SetPriorityClass_addr)
					RELEX::WriteSafe((uintptr_t)SetPriorityClass_addr, { 0x31, 0xC0, 0xC3, 0x90, });
				if (SetProcessAffinityMask_addr)
					RELEX::WriteSafe((uintptr_t)SetProcessAffinityMask_addr, { 0x31, 0xC0, 0xC3, 0x90, });
			}
			else return false;
		}

		// The system does not display the critical-error-handler message box. 
		// Instead, the system sends the error to the calling process.
		// Best practice is that all applications call the process - wide SetErrorMode 
		// function with a parameter of SEM_FAILCRITICALERRORS at startup.
		// This is to prevent error mode dialogs from hanging the application.
		DWORD OldErrMode = 0;
		if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS, &OldErrMode))
		{
			auto ErrorLast = GetLastError();
			REX::ERROR("SetThreadErrorMode returned failed (0x{:x}): {}"sv, ErrorLast, _com_error(ErrorLast).ErrorMessage());
			return false;
		}

		return true;
	}

	bool ModuleThreads::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleThreads::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}