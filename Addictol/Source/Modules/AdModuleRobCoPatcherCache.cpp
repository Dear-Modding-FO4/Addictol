#include <Modules/AdModuleRobCoPatcherCache.h>
#include <AdUtils.h>
#include <RE/T/TESForm.h>

#include <Windows.h>
#include <DbgHelp.h>
#include <REX/W32/VERSION.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesRobCoPatcherCache{ "Others"sv, "bRobCoPatcherCache"sv, true };
	static REX::TOML::Bool<> bRobCoPatcherCacheValidate{ "Additional"sv, "bRobCoPatcherCacheValidate"sv, false };

	namespace robcoDetail
	{
		using TGetForm = RE::TESForm* (*)(const std::string&);

		static TGetForm GetFormOrig = nullptr;
		static std::unordered_map<std::string, RE::TESForm*> g_cache;
		static std::mutex g_lock;
		static std::atomic<bool> g_caching{ false };
		static bool g_validate = false;
		static bool g_done = false;
		static uint64_t g_hits = 0;
		static uint64_t g_misses = 0;
		static double g_missMs = 0.0;

		// Memoizes RobCo Patcher's "Plugin|FormID" resolver; the load order is immutable at runtime.
		static RE::TESForm* GetFormFromIdentifier(const std::string& a_id) noexcept
		{
			if (!g_caching.load(std::memory_order_relaxed))
				return GetFormOrig(a_id);

			std::scoped_lock lock{ g_lock };
			if (auto it = g_cache.find(a_id); it != g_cache.end())
			{
				if (g_validate)
				{
					auto* fresh = GetFormOrig(a_id);
					if (fresh != it->second)
					{
						REX::WARN("RobCo Patcher Cache: mismatch for \"{}\" -- cache disarmed."sv, a_id);
						g_cache.clear();
						g_caching.store(false, std::memory_order_relaxed);
						return fresh;
					}
				}
				++g_hits;
				return it->second;
			}

			const auto t0 = std::chrono::steady_clock::now();
			auto* form = GetFormOrig(a_id);
			g_missMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			++g_misses;
			g_cache.emplace(a_id, form); // maybe try_emplace ???
			return form;
		}

		static void Finish() noexcept
		{
			if (g_done || !GetFormOrig)
				return;
			g_done = true;

			std::scoped_lock lock{ g_lock };
			g_caching.store(false, std::memory_order_relaxed);
			const double saved = g_misses ? static_cast<double>(g_hits) * (g_missMs / static_cast<double>(g_misses)) : 0.0;
			REX::INFO("RobCo Patcher Cache: {} lookups ({} unique), {} hits, resolver {:.1f} ms (saved ~{:.1f} ms)."sv,
				g_hits + g_misses, g_cache.size(), g_hits, g_missMs, saved);
			std::unordered_map<std::string, RE::TESForm*>{}.swap(g_cache);
		}

		static std::string FileVersion(const char* a_path) noexcept
		{
			uint32_t dummy = 0;
			const uint32_t verSize = REX::W32::GetFileVersionInfoSizeA(a_path, &dummy);
			if (!verSize)
				return "unknown";

			auto verBuf = std::make_unique<char[]>(verSize);
			if (!REX::W32::GetFileVersionInfoA(a_path, 0, verSize, verBuf.get()))
				return "unknown";

			void* infoPtr = nullptr;
			uint32_t infoLen = 0;
			if (!REX::W32::VerQueryValueA(verBuf.get(), "\\", &infoPtr, &infoLen) || !infoPtr || infoLen < 52)
				return "unknown";

			const auto info = static_cast<const uint32_t*>(infoPtr);
			if (info[0] != 0xFEEF04BD)
				return "unknown";

			return std::format("{}.{}.{}.{}"sv,
				(info[2] >> 16) & 0xFFFF, info[2] & 0xFFFF,
				(info[3] >> 16) & 0xFFFF, info[3] & 0xFFFF);
		}

		using TSymGetOptions = DWORD(WINAPI*)();
		using TSymSetOptions = DWORD(WINAPI*)(DWORD);
		using TSymInitialize = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
		using TSymLoadModule64 = DWORD64(WINAPI*)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD);
		using TSymFromName = BOOL(WINAPI*)(HANDLE, PCSTR, PSYMBOL_INFO);
		using TSymUnloadModule64 = BOOL(WINAPI*)(HANDLE, DWORD64);
		using TSymCleanup = BOOL(WINAPI*)(HANDLE);

		// Resolves the target from RobCo's own shipped pdb; dbghelp rejects a pdb whose GUID mismatches the dll.
		static uintptr_t ResolveFromPdb(uintptr_t a_base, const char* a_path, const char* a_name) noexcept
		{
			const auto dbghelp = ::LoadLibraryA("dbghelp.dll");
			if (!dbghelp)
			{
				REX::WARN("RobCo Patcher Cache: dbghelp unavailable -- skipping."sv);
				return 0;
			}

			const auto symGetOptions = reinterpret_cast<TSymGetOptions>(::GetProcAddress(dbghelp, "SymGetOptions"));
			const auto symSetOptions = reinterpret_cast<TSymSetOptions>(::GetProcAddress(dbghelp, "SymSetOptions"));
			const auto symInitialize = reinterpret_cast<TSymInitialize>(::GetProcAddress(dbghelp, "SymInitialize"));
			const auto symLoadModule = reinterpret_cast<TSymLoadModule64>(::GetProcAddress(dbghelp, "SymLoadModule64"));
			const auto symFromName = reinterpret_cast<TSymFromName>(::GetProcAddress(dbghelp, "SymFromName"));
			const auto symUnloadModule = reinterpret_cast<TSymUnloadModule64>(::GetProcAddress(dbghelp, "SymUnloadModule64"));
			const auto symCleanup = reinterpret_cast<TSymCleanup>(::GetProcAddress(dbghelp, "SymCleanup"));
			if (!symGetOptions || !symSetOptions || !symInitialize || !symLoadModule || !symFromName || !symUnloadModule || !symCleanup)
			{
				REX::WARN("RobCo Patcher Cache: dbghelp unavailable -- skipping."sv);
				::FreeLibrary(dbghelp);
				return 0;
			}

			char dir[MAX_PATH]{};
			std::snprintf(dir, sizeof(dir), "%s", a_path);
			if (auto* slash = std::strrchr(dir, '\\'))
				*slash = '\0';

			// Private session handle; avoids colliding with crash loggers that SymInitialize the real process.
			const auto session = reinterpret_cast<HANDLE>(uintptr_t{ 0xAD0C0DE });
			const auto oldOptions = symGetOptions();
			symSetOptions(oldOptions | SYMOPT_FAIL_CRITICAL_ERRORS);

			uintptr_t address = 0;
			if (symInitialize(session, dir, FALSE))
			{
				if (const auto mod = symLoadModule(session, nullptr, a_path, nullptr, static_cast<DWORD64>(a_base), 0))
				{
					alignas(SYMBOL_INFO) uint8_t buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
					const auto symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
					symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
					symbol->MaxNameLen = MAX_SYM_NAME;

					static constexpr auto kMangled = "?GetFormFromIdentifier@@YAPEAVTESForm@RE@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z";
					if (symFromName(session, kMangled, symbol) || symFromName(session, "GetFormFromIdentifier", symbol))
						address = static_cast<uintptr_t>(symbol->Address);
					else
						REX::WARN("RobCo Patcher Cache: GetFormFromIdentifier not in {} symbols (pdb missing or mismatched) -- skipping."sv, a_name);

					symUnloadModule(session, mod);
				}
				else
					REX::WARN("RobCo Patcher Cache: could not load symbols for {} -- is the .pdb next to the dll?"sv, a_name);

				symCleanup(session);
			}
			else
				REX::WARN("RobCo Patcher Cache: could not load symbols for {} -- is the .pdb next to the dll?"sv, a_name);

			symSetOptions(oldOptions);
			::FreeLibrary(dbghelp);
			return address;
		}

		static bool InExecutableSection(uintptr_t a_base, uintptr_t a_address) noexcept
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(a_base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
				return false;

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_base + static_cast<uint32_t>(dos->e_lfanew));
			if (nt->Signature != IMAGE_NT_SIGNATURE)
				return false;

			const auto rva = a_address - a_base;
			const auto* section = IMAGE_FIRST_SECTION(nt);
			for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
			{
				if ((section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
					rva >= section[i].VirtualAddress && rva < static_cast<DWORD64>(section[i].VirtualAddress) + section[i].Misc.VirtualSize)
					return true;
			}
			return false;
		}

		// REX prefix, push, or mov starts; 0xE9 means another mod already detoured it (Detours chains fine).
		static bool PlausiblePrologue(uintptr_t a_address) noexcept
		{
			const auto byte = *reinterpret_cast<const uint8_t*>(a_address);
			return (byte >= 0x40 && byte <= 0x57) || (byte >= 0x88 && byte <= 0x8B) || byte == 0xE9;
		}

		static bool InstallHook(bool a_validate) noexcept
		{
			static constexpr const char* kNames[] = { "RobCoPatcherAE.dll", "RobCoPatcherNG.dll", "RobCo_Patcher.dll" };

			HMODULE handle = nullptr;
			const char* name = nullptr;
			for (const auto* candidate : kNames)
			{
				handle = ::GetModuleHandleA(candidate);
				if (handle)
				{
					name = candidate;
					break;
				}
			}
			if (!handle)
			{
				REX::INFO("RobCo Patcher Cache: RobCo Patcher not present."sv);
				return true;
			}

			char path[MAX_PATH]{};
			if (!::GetModuleFileNameA(handle, path, MAX_PATH))
			{
				REX::WARN("RobCo Patcher Cache: could not query the RobCo Patcher module path -- skipping."sv);
				return false;
			}

			const auto base = reinterpret_cast<uintptr_t>(handle);
			const auto address = ResolveFromPdb(base, path, name);
			if (!address)
				return false;

			if (!InExecutableSection(base, address) || !PlausiblePrologue(address))
			{
				REX::WARN("RobCo Patcher Cache: resolved address failed sanity checks -- skipping."sv);
				return false;
			}

			REX::INFO("RobCo Patcher Cache: {} v{} -- GetFormFromIdentifier at +0x{:X}."sv, name, FileVersion(path), address - base);

			GetFormOrig = reinterpret_cast<TGetForm>(RELEX::DetourJump(address, reinterpret_cast<uintptr_t>(&GetFormFromIdentifier)));
			if (!GetFormOrig)
			{
				REX::WARN("RobCo Patcher Cache: detour failed -- skipping."sv);
				return false;
			}

			g_validate = a_validate;
			g_caching.store(true, std::memory_order_relaxed);
			return true;
		}
	}

	ModuleRobCoPatcherCache::ModuleRobCoPatcherCache() :
		Module("RobCo Patcher Cache", &bPatchesRobCoPatcherCache)
	{}

	bool ModuleRobCoPatcherCache::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleRobCoPatcherCache::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return true;

		if (a_msg->type == F4SE::MessagingInterface::kPostLoad)
			return robcoDetail::InstallHook(bRobCoPatcherCacheValidate.GetValue());

		robcoDetail::Finish();
		return true;
	}

	bool ModuleRobCoPatcherCache::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleRobCoPatcherCache::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
