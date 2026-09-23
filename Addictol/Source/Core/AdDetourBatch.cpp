#include <Core/AdDetourBatch.h>
#include <Windows.h>
#include <ms-detours/src/detours.h>
#include <TlHelp32.h>
#include <cstring>
#include <vector>

namespace RELEX
{
	DetourBatchResult DetourBatch(std::span<DetourTarget> a_targets) noexcept
	{
		for (size_t index = 0; index < a_targets.size(); ++index)
		{
			const auto& target = a_targets[index];
			if (!target.original || !target.replacement || target.expected.empty() ||
				std::memcmp(target.original, target.expected.data(), target.expected.size()) != 0)
				return { ERROR_INVALID_DATA, index };
		}
		struct Threads
		{
			std::vector<HANDLE> handles;
			~Threads() { for (auto handle : handles) CloseHandle(handle); }
		} threads;
		const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return { GetLastError(), 0 };
		THREADENTRY32 entry{ sizeof(entry) };
		BOOL found = Thread32First(snapshot, &entry);
		DWORD error = found ? ERROR_SUCCESS : GetLastError();
		while (found)
		{
			if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId())
			{
				auto thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
					FALSE, entry.th32ThreadID);
				if (!thread)
				{
					error = GetLastError();
					break;
				}
				try
				{
					threads.handles.push_back(thread);
				}
				catch (const std::bad_alloc&)
				{
					CloseHandle(thread);
					error = ERROR_NOT_ENOUGH_MEMORY;
					break;
				}
			}
			found = Thread32Next(snapshot, &entry);
			if (!found && GetLastError() != ERROR_NO_MORE_FILES)
				error = GetLastError();
		}
		CloseHandle(snapshot);
		if (error != ERROR_SUCCESS)
			return { error, 0 };
		error = DetourTransactionBegin();
		if (error != NO_ERROR)
			return { error, 0 };
		for (size_t index = 0; index < a_targets.size(); ++index)
		{
			auto& target = a_targets[index];
			if (std::memcmp(target.original, target.expected.data(), target.expected.size()) != 0)
				error = ERROR_INVALID_DATA;
			else
				error = DetourAttach(&target.original, target.replacement);
			if (error != NO_ERROR)
			{
				DetourTransactionAbort();
				return { error, index };
			}
		}
		// Prepare allocations before suspending threads, then recheck bytes before publishing.
		for (auto thread : threads.handles)
		{
			error = DetourUpdateThread(thread);
			if (error != NO_ERROR)
			{
				DetourTransactionAbort();
				return { error, 0 };
			}
		}
		for (size_t index = 0; index < a_targets.size(); ++index)
		{
			const auto& target = a_targets[index];
			if (std::memcmp(target.original, target.expected.data(), target.expected.size()) != 0)
			{
				DetourTransactionAbort();
				return { ERROR_INVALID_DATA, index };
			}
		}
		PVOID* failed{};
		error = DetourTransactionCommitEx(&failed);
		size_t index{};
		for (; index < a_targets.size(); ++index)
			if (&a_targets[index].original == failed)
				break;
		return { error, index };
	}
}
