#include <AdProfilerDLL.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>

#include <Windows.h>

// Resolve macro conflicts between Windows.h and CommonLibF4 identifiers.
#undef ERROR
#undef MEM_RELEASE
#undef MAX_PATH
#undef PAGE_EXECUTE_READWRITE

#include <REX/W32/KERNEL32.h>
#include <REX/W32/VERSION.h>

#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>

namespace Addictol
{
	// F4SE resolves and calls plugin exports sequentially on the main thread, so hook state needs no locking.

	using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
	static GetProcAddress_t s_origGetProcAddress = nullptr;

	static HMODULE s_ownModule = nullptr;

	// OG F4SE queries every plugin before loading any, so entries must survive between phases.
	static std::unordered_map<HMODULE, DLLProfileEntry> s_pendingEntries;

	static HMODULE s_activeModule = nullptr;
	static FARPROC s_origQueryFn  = nullptr;
	static FARPROC s_origLoadFn   = nullptr;

	static std::string GetModulePath(HMODULE a_module) noexcept
	{
		char buf[4096]{};
		if (REX::W32::GetModuleFileNameA(
			reinterpret_cast<REX::W32::HMODULE>(a_module), buf,
			static_cast<uint32_t>(sizeof(buf))))
		{
			return buf;
		}
		return {};
	}

	static std::string ExtractFileName(const std::string& a_path) noexcept
	{
		auto pos = a_path.find_last_of("\\/"sv);
		return (pos != std::string::npos) ? a_path.substr(pos + 1) : a_path;
	}

	static DLLProfileEntry& GetOrCreateEntry(HMODULE a_module) noexcept
	{
		auto it = s_pendingEntries.find(a_module);
		if (it != s_pendingEntries.end())
			return it->second;

		auto path = GetModulePath(a_module);
		auto name = ExtractFileName(path);
		auto ver  = ProfilerDLL::GetFileVersionString(path.c_str());

		auto& entry       = s_pendingEntries[a_module];
		entry.dllName     = std::move(name);
		entry.dllPath     = std::move(path);
		entry.fileVersion = std::move(ver);
		return entry;
	}

	// Opaque parameters decouple the wrappers from F4SE types when cast through FARPROC.

	static bool Wrapper_Query(const void* a_f4se, void* a_info) noexcept
	{
		HMODULE mod    = s_activeModule;
		auto    origFn = reinterpret_cast<bool(*)(const void*, void*)>(s_origQueryFn);

		auto start  = std::chrono::high_resolution_clock::now();
		bool result = origFn(a_f4se, a_info);
		auto end  = std::chrono::high_resolution_clock::now();

		double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

		auto it = s_pendingEntries.find(mod);
		if (it != s_pendingEntries.end())
		{
			it->second.queryMs = elapsed;
			REX::INFO("[Profiler] DLL Query: {} ({:.2f} ms)"sv,
				it->second.dllName, elapsed);
		}

		return result;
	}

	static bool Wrapper_Load(const void* a_f4se) noexcept
	{
		HMODULE mod    = s_activeModule;
		auto    origFn = reinterpret_cast<bool(*)(const void*)>(s_origLoadFn);

		auto start  = std::chrono::high_resolution_clock::now();
		bool result = origFn(a_f4se);
		auto end    = std::chrono::high_resolution_clock::now();

		double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

		auto it = s_pendingEntries.find(mod);
		if (it != s_pendingEntries.end())
		{
			it->second.loadMs = elapsed;
			REX::INFO("[Profiler] DLL Load: {} ({:.2f} ms)"sv,
				it->second.dllName, elapsed);

			ProfilerCore::GetSingleton()->AddDLLEntry(std::move(it->second));
			s_pendingEntries.erase(it);
		}

		return result;
	}

	// Only calls through F4SE's IAT reach this hook; other modules are unaffected.

	static FARPROC WINAPI Hooked_GetProcAddress(HMODULE a_module, LPCSTR a_procName) noexcept
	{
		// HIWORD == 0 denotes an ordinal import rather than a string.
		if ((reinterpret_cast<uintptr_t>(a_procName) >> 16) == 0)
			return s_origGetProcAddress(a_module, a_procName);

		if (a_module == s_ownModule)
			return s_origGetProcAddress(a_module, a_procName);

		if (std::strcmp(a_procName, "F4SEPlugin_Query") == 0)
		{
			FARPROC original = s_origGetProcAddress(a_module, a_procName);
			if (original)
			{
				s_activeModule = a_module;
				s_origQueryFn  = original;
				GetOrCreateEntry(a_module);
				return reinterpret_cast<FARPROC>(&Wrapper_Query);
			}
			return original; // nullptr -- DLL does not export this symbol
		}

		if (std::strcmp(a_procName, "F4SEPlugin_Load") == 0)
		{
			FARPROC original = s_origGetProcAddress(a_module, a_procName);
			if (original)
			{
				s_activeModule = a_module;
				s_origLoadFn   = original;
				GetOrCreateEntry(a_module);
				return reinterpret_cast<FARPROC>(&Wrapper_Load);
			}
			return original;
		}

		return s_origGetProcAddress(a_module, a_procName);
	}

	void ProfilerDLL::Install(const F4SE::LoadInterface* a_f4se) noexcept
	{
		if (m_installed)
			return;

		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive())
			return;

		// The interface resides in F4SE's data section, so its address identifies the owning module.
		HMODULE hF4SE = nullptr;
		::GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(a_f4se),
			&hF4SE);

		if (!hF4SE)
		{
			REX::WARN("[Profiler] DLL: Failed to resolve F4SE module handle"sv);
			return;
		}

		::GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&Hooked_GetProcAddress),
			&s_ownModule);

		auto original = RELEX::DetourIAT(
			reinterpret_cast<uintptr_t>(hF4SE),
			"kernel32.dll",
			"GetProcAddress",
			reinterpret_cast<uintptr_t>(&Hooked_GetProcAddress));

		if (!original)
		{
			REX::WARN("[Profiler] DLL: Failed to hook GetProcAddress in F4SE IAT"sv);
			return;
		}

		s_origGetProcAddress = reinterpret_cast<GetProcAddress_t>(original);
		m_installed = true;

		REX::INFO("[Profiler] DLL: GetProcAddress hook installed in F4SE IAT"sv);
		profiler->MarkPhase("DLLProfilerInstalled"sv);
	}

	// The fixed version block avoids language- and code-page-dependent StringFileInfo lookup.

	std::string ProfilerDLL::GetFileVersionString(const char* a_path) noexcept
	{
		if (!a_path || !*a_path)
			return {};

		uint32_t dummy = 0;
		uint32_t verSize = REX::W32::GetFileVersionInfoSizeA(a_path, &dummy);
		if (!verSize)
			return {};

		auto verBuf = std::make_unique<char[]>(verSize);
		if (!REX::W32::GetFileVersionInfoA(a_path, 0, verSize, verBuf.get()))
			return {};

		void*         infoPtr = nullptr;
		uint32_t infoLen = 0;
		if (!REX::W32::VerQueryValueA(verBuf.get(), "\\", &infoPtr, &infoLen))
			return {};

		// VS_FIXEDFILEINFO is 13 DWORDs, starts with 0xFEEF04BD, and stores version words at indices 2 and 3.
		constexpr uint32_t kFixedInfoMinSize = 52;
		if (!infoPtr || infoLen < kFixedInfoMinSize)
			return {};

		auto info = static_cast<const uint32_t*>(infoPtr);

		constexpr uint32_t kSignature = 0xFEEF04BD;
		if (info[0] != kSignature)
			return {};

		uint32_t ms = info[2];
		uint32_t ls = info[3];

		return std::format("{}.{}.{}.{}"sv,
			(ms >> 16) & 0xFFFF,
			(ms)       & 0xFFFF,
			(ls >> 16) & 0xFFFF,
			(ls)       & 0xFFFF);
	}
}
