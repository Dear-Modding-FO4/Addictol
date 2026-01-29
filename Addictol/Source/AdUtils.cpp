#include <Windows.h>
#include <REL\Utility.h>
#include <AdUtils.h>
#include <AdAssert.h>

std::string AdGetRuntimePath() noexcept
{
    static char	appPath[4096] = { 0 };

    if (appPath[0])
        return appPath;

    AdAssert(GetModuleFileName(GetModuleHandle(NULL), appPath, sizeof(appPath)));
    return appPath;
}

std::string AdGetRuntimeDirectory() noexcept
{
    std::string runtimeDirectory;

    if (runtimeDirectory.empty())
    {
        std::string	runtimePath = AdGetRuntimePath();
        // truncate at last slash
        std::string::size_type	lastSlash = runtimePath.rfind('\\');
        if (lastSlash != std::string::npos)
            // if we don't find a slash something is VERY WRONG
            runtimeDirectory = runtimePath.substr(0, lastSlash + 1);
    }

    return runtimeDirectory;
}

namespace RELEX
{
	ScopeLock::ScopeLock(uintptr_t a_target, uintptr_t a_size) noexcept :
		_locked(false), _old(0), _target(a_target), _size(a_size)
	{
		_locked = VirtualProtect(reinterpret_cast<void*>(a_target), (SIZE_T)a_size, PAGE_EXECUTE_READWRITE, (PDWORD)&_old);
	}

	ScopeLock::ScopeLock(void* a_target, uintptr_t a_size) noexcept :
		_locked(false), _old(0), _target((uintptr_t)a_target), _size(a_size)
	{
		_locked = VirtualProtect(a_target, (SIZE_T)a_size, PAGE_EXECUTE_READWRITE, (PDWORD)&_old);
	}

	ScopeLock::~ScopeLock() noexcept
	{
		if (_locked)
		{
			// Ignore if this fails, the memory was copied either way
			VirtualProtect(reinterpret_cast<void*>(_target), (SIZE_T)_size, _old, (PDWORD)&_old);
			FlushInstructionCache(GetCurrentProcess(), (LPVOID)_target, _size);
			_locked = false;
		}
	}

	void WriteSafe(uintptr_t a_target, std::initializer_list<uint8_t> a_data) noexcept
	{
		if (!a_target || !a_data.size()) return;

		ScopeLock Lock(a_target, a_data.size());
		if (Lock.HasUnlocked())
		{
			uintptr_t i = a_target;
			for (auto value : a_data)
				*(volatile uint8_t*)i++ = value;
		}
	}

	void WriteSafeNop(uintptr_t a_target, size_t a_size) noexcept
	{
		if (!a_target || !a_size) return;
		ScopeLock Lock(a_target, a_size);
		if (Lock.HasUnlocked())
			memset((LPVOID)a_target, 0x90, a_size);
	}
}