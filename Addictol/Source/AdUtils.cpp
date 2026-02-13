#include <REL\Module.h>
#include <REL\Utility.h>
#include <AdUtils.h>
#include <AdAssert.h>
#include <detours\Detours.h>
#include <windows.h>

#include <INI\SimpleIni.h>

#undef ERROR
#undef MEM_RELEASE
#undef MAX_PATH
#undef PAGE_EXECUTE_READWRITE

std::string AdGetRuntimePath() noexcept
{
    static char	appPath[4096] = { 0 };

    if (appPath[0])
        return appPath;

    AdAssert(REX::W32::GetModuleFileNameA(REX::W32::GetModuleHandleA(nullptr), appPath, sizeof(appPath)));
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
		_unlocked(false), _old(0), _target(a_target), _size(a_size)
	{
		_unlocked = REX::W32::VirtualProtect(reinterpret_cast<void*>(a_target), (size_t)a_size,
			REX::W32::PAGE_EXECUTE_READWRITE, (uint32_t*)&_old);
	}

	ScopeLock::ScopeLock(void* a_target, uintptr_t a_size) noexcept :
		_unlocked(false), _old(0), _target((uintptr_t)a_target), _size(a_size)
	{
		_unlocked = REX::W32::VirtualProtect(a_target, (size_t)a_size, REX::W32::PAGE_EXECUTE_READWRITE, (uint32_t*)&_old);
	}

	ScopeLock::~ScopeLock() noexcept
	{
		if (_unlocked)
		{
			// Ignore if this fails, the memory was copied either way
			REX::W32::VirtualProtect(reinterpret_cast<void*>(_target), (size_t)_size, _old, (uint32_t*)&_old);
			REX::W32::FlushInstructionCache(REX::W32::GetCurrentProcess(), (void*)_target, _size);
			_unlocked = false;
		}
	}

	void Write(uintptr_t a_target, const std::initializer_list<uint8_t>& a_data) noexcept
	{
		if (!a_target || !a_data.size()) return;
		REL::Write(a_target, (const void*)a_data.begin(), a_data.size());
	}

	void WriteNop(uintptr_t a_target, size_t a_size) noexcept
	{
		if (!a_target || !a_size) return;
		std::fill_n(reinterpret_cast<std::uint8_t*>(a_target), a_size, REL::NOP);
	}

	void WriteSafe(uintptr_t a_target, const std::initializer_list<uint8_t>& a_data) noexcept
	{
		if (!a_target || !a_data.size()) return;

		ScopeLock Lock(a_target, a_data.size());
		if (Lock.HasUnlocked())
			REL::Write(a_target, (const void*)a_data.begin(), a_data.size());
	}

	void WriteSafeNop(uintptr_t a_target, size_t a_size) noexcept
	{
		if (!a_target || !a_size) return;
		ScopeLock Lock(a_target, a_size);
		if (Lock.HasUnlocked())
			std::fill_n(reinterpret_cast<std::uint8_t*>(a_target), a_size, REL::NOP);
	}

	bool IsRuntimeOG() noexcept
	{
		return REL::Module::IsRuntimeOG();
	}

	bool IsRuntimeNG() noexcept
	{
		return REL::Module::IsRuntimeNG();
	}

	bool IsRuntimeAE() noexcept
	{
		return REL::Module::IsRuntimeAE();
	}

	uintptr_t DetourJump(uintptr_t a_target, uintptr_t a_function) noexcept
	{
		return Detours::X64::DetourFunction(a_target, a_function, Detours::X64Option::USE_REL32_JUMP);
	}

	uintptr_t DetourCall(uintptr_t a_target, uintptr_t a_function) noexcept
	{
		return Detours::X64::DetourFunction(a_target, a_function, Detours::X64Option::USE_REL32_CALL);
	}

	uintptr_t DetourVTable(uintptr_t a_target, uintptr_t a_function, uint32_t a_index) noexcept
	{
		return Detours::X64::DetourVTable(a_target, a_function, a_index);
	}

	uintptr_t DetourIAT(const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept
	{
		return Detours::IATHook(REL::Module::GetSingleton()->base(), a_importModule, a_functionName, a_function);
	}

	uintptr_t DetourIAT(uintptr_t a_targetModule, const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept
	{
		return Detours::IATHook(a_targetModule, a_importModule, a_functionName, a_function);
	}

	uintptr_t DetourIATDelayed(const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept
	{
		return Detours::IATDelayedHook(REL::Module::GetSingleton()->base(), a_importModule, a_functionName, a_function);
	}

	uintptr_t DetourIATDelayed(uintptr_t a_targetModule, const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept
	{
		return Detours::IATDelayedHook(a_targetModule, a_importModule, a_functionName, a_function);
	}
}

namespace Addictol
{
	uint32_t Extend16(uint32_t a_in) noexcept
	{
		return (a_in & 0x8000) ? (0xFFFF0000 | a_in) : a_in;
	}

	uint32_t Extend8(uint32_t a_in) noexcept
	{
		return (a_in & 0x80) ? (0xFFFFFF00 | a_in) : a_in;
	}

	uint16_t Swap16(uint16_t a_in) noexcept
	{
		return	((a_in >> 8) & 0x00FF) |
			((a_in << 8) & 0xFF00);
	}

	uint32_t Swap32(uint32_t a_in) noexcept
	{
		return	((a_in >> 24) & 0x000000FF) |
			((a_in >> 8) & 0x0000FF00) |
			((a_in << 8) & 0x00FF0000) |
			((a_in << 24) & 0xFF000000);
	}

	uint64_t Swap64(uint64_t a_in) noexcept
	{
		uint64_t temp;

		temp = Swap64(a_in);
		temp <<= 32;
		temp |= Swap64(a_in >> 32);

		return temp;
	}

	void SwapFloat(float* a_in) noexcept
	{
		uint32_t* temp = (uint32_t*)a_in;
		*temp = Swap32(*temp);
	}

	void SwapDouble(double* a_in) noexcept
	{
		uint64_t* temp = (uint64_t*)a_in;
		*temp = Swap64(*temp);
	}

	bool IsBigEndian() noexcept
	{
		union
		{
			uint16_t	u16;
			uint8_t		u8[2];
		} temp{};

		temp.u16 = 0x1234;

		return temp.u8[0] == 0x12;
	}

	bool IsLittleEndian() noexcept
	{
		return !IsBigEndian();
	}

	std::string& Trim(std::string& String) noexcept
	{
		constexpr static char whitespaceDelimiters[] = " \t\n\r\f\v";

		if (!String.empty())
		{
			String.erase(String.find_last_not_of(whitespaceDelimiters) + 1);
			String.erase(0, String.find_first_not_of(whitespaceDelimiters));
		}

		return String;
	}

	bool WriteINISettingInt(const wchar_t* a_INIFile, const wchar_t* a_optionName, long a_value) noexcept
	{
		if (!a_INIFile || !a_optionName)
			return false;

		return WriteINISettingString(a_INIFile, a_optionName, std::to_wstring(a_value).c_str());
	}

	bool WriteINISettingFloat(const wchar_t* a_INIFile, const wchar_t* a_optionName, float a_value) noexcept
	{
		if (!a_INIFile || !a_optionName)
			return false;

		return WriteINISettingString(a_INIFile, a_optionName, std::to_wstring(a_value).c_str());
	}

	bool WriteINISettingString(const wchar_t* a_INIFile, const wchar_t* a_optionName, const wchar_t* a_value) noexcept
	{
		if (!a_INIFile || !a_optionName || !a_value)
			return false;

		std::wstring op = a_optionName;
		auto it = op.find_first_of(L':');
		if (it == std::wstring::npos)
			return false;

		std::wstring sec = op.substr(it + 1);
		op = op.substr(0, it);

		auto proc = *(uintptr_t*)REL::GetIATAddr("kernel32.dll"sv, "WritePrivateProfileStringA"sv);
		if (*(uint8_t*)proc == 0xE9)
		{
			// Inject detected, maybe PrivateProfileRedirector F4 mod

			CSimpleIniW ini;
			ini.LoadFile(a_INIFile);
			ini.SetValue(sec.c_str(), op.c_str(), a_value, nullptr, true);
			ini.SaveFile(a_INIFile);

			// PrivateProfileRedirector F4 mod this also hooked
			// Write to cache mod
			// return WritePrivateProfileStringW(sec.c_str(), op.c_str(), a_value, a_INIFile);
		}
		
		return WritePrivateProfileStringW(sec.c_str(), op.c_str(), a_value, a_INIFile);
	}

	std::string WideToSysChar(const std::wstring& s) noexcept
	{
		if (s.empty() || !s.length())
			return "";

		int len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.length(), nullptr, 0, nullptr, nullptr);
		if (len > 0)
		{
			auto buf = std::make_unique<char[]>((size_t)len + 1);
			std::fill_n(buf.get(), (size_t)len + 1, 0);
			WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.length(), buf.get(), len, nullptr, nullptr);
			return buf.get();
		}

		return "";
	}

	std::wstring SysCharToWide(const std::string& s) noexcept
	{
		if (s.empty() || !s.length())
			return L"";

		int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.length(), nullptr, 0);
		if (len > 0)
		{
			auto buf = std::make_unique<wchar_t[]>((size_t)len + 1);
			std::fill_n(buf.get(), (size_t)len + 1, 0);
			MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.length(), buf.get(), len);
			return buf.get();
		}

		return L"";
	}

	// Added F4SE 0.7.1+
	[[nodiscard]] const char* GetSaveFolderName() noexcept
	{
		return "Fallout4";
	}
}