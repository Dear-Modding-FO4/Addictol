#include <AdAssert.h>
#include <AdAllocator.h>
#include <REX/REX.h>
#include <windows.h>

namespace Addictol
{
	// Use to convert bytes to GB
	constexpr static auto AD_DIV = 1024 * 1024 * 1024;

	namespace
	{
		HeapKind s_selectedHeapKind = HeapKind::Voltek;
	}

	bool ResolveHeapSelection(std::string_view a_name) noexcept
	{
		for (const auto& heap : HEAP_NAMES)
		{
			if (heap.name == a_name)
			{
				s_selectedHeapKind = heap.kind;
				return true;
			}
		}

		REX::WARN("Unknown or unavailable allocator backend \"{}\"; valid values are: voltek", a_name);
		return false;
	}

	HeapKind GetSelectedHeapKind() noexcept
	{
		return s_selectedHeapKind;
	}

	void* ICheckerPointer::CheckPtr(void* lpBlock, size_t nSize) const noexcept
	{
		if (!lpBlock && nSize)
		{
			static MEMORYSTATUSEX statex = { 0 };
			statex.dwLength = sizeof(MEMORYSTATUSEX);
			if (!GlobalMemoryStatusEx(&statex))
				return lpBlock;

			//  https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex
			//  Sample output:
			//  OUT OF MEMORY. A memory allocation failed.
			//  
			//  Requested chunk size: 123456 bytes.
			//  There is     51 percent of memory in use.
			//  There are 20.29 total GB of physical memory.
			//  There are  9.87 free  GB of physical memory.
			//  There are 38.84 total GB of paging file.
			//  There are 27.99 free  GB of paging file.
			AdAssertWithFormattedMessage(lpBlock,
				"OUT OF MEMORY. A memory allocation failed.\n\n"
				"Requested chunk size: %llu bytes.\n"
				"There is %ld percent of memory in use.\n"
				"There are %.2f total GB of physical memory.\n"
				"There are %.2f free GB of physical memory.\n"
				"There are %.2f total GB of paging file.\n"
				"There are %.2f free GB of paging file.\n",
				nSize, statex.dwMemoryLoad,
				(long double)statex.ullTotalPhys / AD_DIV,		(long double)statex.ullAvailPhys / AD_DIV,
				(long double)statex.ullTotalPageFile / AD_DIV,	(long double)statex.ullAvailPageFile / AD_DIV);
		}

		return lpBlock;
	}
}