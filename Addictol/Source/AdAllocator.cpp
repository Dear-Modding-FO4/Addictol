#include <AdAssert.h>
#include <AdAllocator.h>
#include <REX\REX\TOML.h>
#include <windows.h>

namespace Addictol
{
	void* ICheckerPointer::CheckPtr(void* lpBlock, size_t nSize) const noexcept
	{
		if (!lpBlock)
		{
			static MEMORYSTATUSEX statex = { 0 };
			statex.dwLength = sizeof(MEMORYSTATUSEX);
			if (!GlobalMemoryStatusEx(&statex))
				return lpBlock;

			AdAssertWithFormattedMessage(lpBlock,
				"A memory allocation failed.\n\nRequested chunk size: %llu bytes.\n\nAvail memory: %llu bytes, load (%u%%).",
				nSize, statex.ullAvailPageFile, statex.dwMemoryLoad);
		}

		return lpBlock;
	}
}