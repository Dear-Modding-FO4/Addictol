#include <AdProfilerESP.h>
#include <AdProfilerESPCompileFiles.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif

namespace
{
	using namespace Addictol;

	// MSVC C2712 forbids non-trivial local objects in functions that use __try.

	// Returns 0 on exception, 1 for true, and 2 for false.
	int SafeCallCompileFiles(
		bool(__fastcall* a_original)(void*, bool),
		void* a_this,
		bool a_load) noexcept
	{
		__try
		{
			return a_original(a_this, a_load) ? 1 : 2;
		}
		__except (1)
		{
			return 0;
		}
	}

	bool SafeCallConstructObjectList(void(__fastcall* a_original)(void*, void*, bool, void*),
		void* a_this, void* a_file, bool a_isFirst, void* a_param4) noexcept
	{
		__try
		{
			a_original(a_this, a_file, a_isFirst, a_param4);
			return true;
		}
		__except (1)
		{
			return false;
		}
	}

	bool SafeCallInitAllForms(void(__fastcall* a_original)(void*), void* a_this) noexcept
	{
		__try
		{
			a_original(a_this);
			return true;
		}
		__except (1)
		{
			return false;
		}
	}

	std::size_t ScanCallSitesImpl(
		uintptr_t a_funcBase, std::size_t a_maxBytes,
		ESPProfiler::CallSiteInfo* a_outBuf, std::size_t a_maxResults) noexcept
	{
		std::size_t count = 0;

		__try
		{
			for (std::size_t i = 0; i + 5 <= a_maxBytes && count < a_maxResults; ++i)
			{
				if (*reinterpret_cast<const uint8_t*>(a_funcBase + i) != 0xE8)
					continue;

				// E8 target = opcode address + 5-byte instruction length + signed rel32.
				auto disp = *reinterpret_cast<const std::int32_t*>(a_funcBase + i + 1);
				uintptr_t site = a_funcBase + i;
				uintptr_t target = site + 5 + static_cast<std::intptr_t>(disp);

				// Nearby executable targets reject most immediate operands that merely contain 0xE8.
				auto diff = static_cast<std::intptr_t>(target - site);
				if (target > 0x10000 && diff > -0x40000000LL && diff < 0x40000000LL)
					a_outBuf[count++] = { site, target, i };
			}
		}
		__except (1) {}

		return count;
	}
}

namespace Addictol
{
	static REX::TOML::F64<> fWarnThresholdMs{ "Profiler"sv, "fWarnThresholdMs"sv, 500.0 };
	static REX::TOML::F64<> fCritThresholdMs{ "Profiler"sv, "fCritThresholdMs"sv, 2000.0 };
	static REX::TOML::Bool<> bESPSubHooks{ "Profiler"sv, "bESPSubHooks"sv, false };

	// Sub-hook RVAs are Fallout4.exe-relative and verified by Ghidra, F4LoadTimeProfiler, and NG PDB analysis.
	// OG 1.10.163: ConstructObjectList 0x118750 is 594 bytes with four parameters.
	// OG 1.10.163: InitAllForms 0x11B070 is 2,116 bytes with one parameter.
	// NG 1.11.191: ConstructObjectList 0x2DFA40 is absent from PDB publics and confirmed by F4LoadTimeProfiler.
	// NG 1.11.191: InitAllForms 0x2EC830 is ?InitAllForms@TESDataHandler@@QEAAXXZ at section 1 offset 0x2EB830 with .text at 0x1000.
	// Prior candidate 0x2EB570 is SetMasterFileLargeBuffer, not InitAllForms.
	// NG CompileFiles takes about 50 ms instead of OG's 10 seconds because form loading moved to a deferred path.
	// NG ConstructObjectList grew from about 594 to 13,600 bytes and runs once with a placeholder instead of per file.
	// NG CheckModsLoaded grew from about 115 to 3,984 bytes, indicating redistributed loading orchestration.
	// NG's 3.5-second CompileFiles_End-to-GameDataReady interval corresponds to OG's in-CompileFiles form loading.
	static constexpr uintptr_t kOG_ConstructObjectList_RVA = 0x118750;
	static constexpr uintptr_t kOG_InitAllForms_RVA        = 0x11B070;
	static constexpr uintptr_t kNG_ConstructObjectList_RVA  = 0x2DFA40;
	static constexpr uintptr_t kNG_InitAllForms_RVA         = 0x2EC830; // PDB-confirmed

	// NG ConstructObject at 0x2EA240 is per-form rather than per-file.
	// Signature: bool ConstructObject(TESFile*, bool, TESForm*, bool)
	// PDB: ?ConstructObject@TESDataHandler@@QEAA_NPEAVTESFile@@_NPEAVTESForm@@1@Z
	static constexpr uintptr_t kNG_ConstructObject_RVA      = 0x2EA240;

	// TESFile::filename is an inline char[260] at +0x70.
	static constexpr uintptr_t kTESFileNameOffset = 0x70;

	std::vector<ESPProfiler::CallSiteInfo> ESPProfiler::ScanCallSites(
		uintptr_t a_funcBase, std::size_t a_maxBytes) noexcept
	{
		// A 256-entry stack buffer covers 4 KB of code while using about 6 KB.
		static constexpr std::size_t kMaxResults = 256;
		CallSiteInfo buffer[kMaxResults]{};

		auto count = ScanCallSitesImpl(a_funcBase, a_maxBytes, buffer, kMaxResults);
		return { buffer, buffer + count };
	}

	const char* ESPProfiler::GetTESFileName(void* a_file) noexcept
	{
		if (!a_file)
			return nullptr;

		__try
		{
			auto addr = reinterpret_cast<uintptr_t>(a_file) + kTESFileNameOffset;
			const char* name = reinterpret_cast<const char*>(addr);

			if (name[0] > 0x1F && name[0] < 0x7F)
				return name;
		}
		__except (1)
		{
		}

		return nullptr;
	}

	bool __fastcall ESPProfiler::HookCompileFiles(void* a_this, bool a_load) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		if (!core->IsActive() || !OriginalCompileFiles)
			return OriginalCompileFiles ? OriginalCompileFiles(a_this, a_load) : false;

		REX::INFO("[Profiler/ESP] CompileFiles entered (this={:016X})"sv,
			reinterpret_cast<uintptr_t>(a_this));

		GetSingleton()->m_currentFileIndex = 0;

		core->MarkPhase("CompileFiles_Begin"sv);

		auto start = std::chrono::high_resolution_clock::now();
		int callResult = SafeCallCompileFiles(OriginalCompileFiles, a_this, a_load);
		auto end = std::chrono::high_resolution_clock::now();
		double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

		if (callResult == 0)
		{
			REX::ERROR("[Profiler/ESP] CompileFiles CRASHED in original function! "
				"SEH caught the exception. Returning false."sv);
			return false;
		}

		bool result = (callResult == 1);
		core->SetTotalCompileTime(totalMs);
		core->MarkPhase("CompileFiles_End"sv);

		REX::INFO("[Profiler/ESP] CompileFiles completed in {:.1f} ms ({:.2f} s)"sv,
			totalMs, totalMs / 1000.0);

		return result;
	}

	void __fastcall ESPProfiler::HookConstructObjectList(void* a_this, void* a_file, bool a_isFirst, void* a_param4) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();
		auto* self = GetSingleton();

		if (!core->IsActive() || !OriginalConstructObjectList)
		{
			if (OriginalConstructObjectList)
				OriginalConstructObjectList(a_this, a_file, a_isFirst, a_param4);
			return;
		}

		ESPProfileEntry entry;
		entry.loadOrderIndex = self->m_currentFileIndex++;

		const char* name = GetTESFileName(a_file);
		entry.filename = name ? name : "(unknown)"sv;

		auto start = std::chrono::high_resolution_clock::now();
		bool ok = SafeCallConstructObjectList(OriginalConstructObjectList, a_this, a_file, a_isFirst, a_param4);
		auto end = std::chrono::high_resolution_clock::now();
		entry.constructMs = std::chrono::duration<double, std::milli>(end - start).count();

		if (!ok)
		{
			REX::ERROR("[Profiler/ESP] ConstructObjectList CRASHED on file [{}] {}!"sv,
				entry.loadOrderIndex, entry.filename);
			return;
		}

		entry.totalMs = entry.constructMs;

		const double critMs = fCritThresholdMs.GetValue();
		const double warnMs = fWarnThresholdMs.GetValue();

		if (entry.constructMs >= critMs)
		{
			REX::WARN("[Profiler/ESP] CRITICAL: [{:3d}] {:40s} {:.1f} ms"sv,
				entry.loadOrderIndex, entry.filename, entry.constructMs);
		}
		else if (entry.constructMs >= warnMs)
		{
			REX::WARN("[Profiler/ESP] SLOW: [{:3d}] {:40s} {:.1f} ms"sv,
				entry.loadOrderIndex, entry.filename, entry.constructMs);
		}

		core->AddESPEntry(std::move(entry));
	}

	void __fastcall ESPProfiler::HookInitAllForms(void* a_this) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		if (!core->IsActive() || !OriginalInitAllForms)
		{
			if (OriginalInitAllForms)
				OriginalInitAllForms(a_this);
			return;
		}

		core->MarkPhase("InitAllForms_Begin"sv);

		auto start = std::chrono::high_resolution_clock::now();
		bool ok = SafeCallInitAllForms(OriginalInitAllForms, a_this);
		auto end = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(end - start).count();

		if (!ok)
		{
			REX::ERROR("[Profiler/ESP] InitAllForms CRASHED! SEH caught the exception."sv);
			return;
		}

		core->SetInitAllFormsTime(ms);
		core->MarkPhase("InitAllForms_End"sv);

		REX::INFO("[Profiler/ESP] InitAllForms completed in {:.1f} ms ({:.2f} s)"sv,
			ms, ms / 1000.0);
	}

	void ESPProfiler::Install() noexcept
	{
		if (m_installed)
			return;

		if (!ProfilerCore::GetSingleton()->IsActive())
		{
			REX::INFO("[Profiler/ESP] Profiler not active, skipping ESP hooks"sv);
			return;
		}

		REX::INFO("[Profiler/ESP] Installing ESP/ESM load profiler hooks..."sv);

		const bool isOG = RELEX::IsRuntimeOG();
		const auto runtime = isOG ? ESPCompileFiles::Runtime::OG :
			(RELEX::IsRuntimeAE() ? ESPCompileFiles::Runtime::AE : ESPCompileFiles::Runtime::NG);
		const auto detectedRuntime = isOG ? "OG"sv : (RELEX::IsRuntimeAE() ? "AE"sv : "NG"sv);
		const auto& target = ESPCompileFiles::GetTarget(runtime);
		const auto compileFilesAddr = REL::ID{
			ESPCompileFiles::GetTarget(ESPCompileFiles::Runtime::OG).id,
			ESPCompileFiles::GetTarget(ESPCompileFiles::Runtime::NG).id,
			ESPCompileFiles::GetTarget(ESPCompileFiles::Runtime::AE).id
		}.address();

		const auto code = std::span{
			reinterpret_cast<const std::uint8_t*>(compileFilesAddr),
			target.signature.size()
		};
		REX::INFO(
			"[Profiler/ESP] CompileFiles target: runtime {}, slot {}, id {}, address {:016X}."sv,
			detectedRuntime,
			target.slot,
			target.id,
			compileFilesAddr);
		if (!ESPCompileFiles::Matches(code, target))
		{
			REX::ERROR(
				"[Profiler/ESP] CompileFiles exact signature mismatch: runtime {}, slot {}, id {}, address {:016X}; installing nothing."sv,
				detectedRuntime,
				target.slot,
				target.id,
				compileFilesAddr);
			return;
		}

		*(uintptr_t*)(&OriginalCompileFiles) =
			RELEX::DetourJump(compileFilesAddr, (uintptr_t)&HookCompileFiles);

		if (!OriginalCompileFiles)
		{
			REX::ERROR(
				"[Profiler/ESP] CompileFiles detour failed after exact validation; target may be left in an indeterminate state."sv);
			return;
		}

		REX::INFO("[Profiler/ESP] CompileFiles hooked (trampoline: {:016X})"sv,
			reinterpret_cast<uintptr_t>(OriginalCompileFiles));

		// Diagnostic E8 sites help identify new builds; hook installation never patches these call sites.

		static constexpr std::size_t kMaxScanBytes = 0x1000;
		auto callSites = ScanCallSites(compileFilesAddr, kMaxScanBytes);

		REX::INFO("[Profiler/ESP] Diagnostic: {} call sites in CompileFiles ({} bytes):"sv,
			callSites.size(), kMaxScanBytes);

		for (std::size_t i = 0; i < callSites.size(); ++i)
		{
			REX::INFO("[Profiler/ESP] [{:2d}] site={:016X} target={:016X} (offset + 0x{:04X})"sv,
				i, callSites[i].site, callSites[i].target, callSites[i].offset);
		}

		// Sub-hooks use function-entry DetourJump at verified RVAs because they lack Address Library IDs.

		if (!bESPSubHooks.GetValue())
		{
			REX::INFO("[Profiler/ESP] Sub-hooks disabled (bESPSubHooks=false). "
				"Only CompileFiles timing active."sv);
		}
		else if (!isOG)
		{
			REX::WARN(
				"[Profiler/ESP] Sub-hooks unavailable on {}: targets are not identified for this runtime. Only CompileFiles timing is active."sv,
				detectedRuntime);
		}
		else
		{
			HMODULE hGame = GetModuleHandleA("Fallout4.exe");
			if (!hGame)
			{
				REX::WARN("[Profiler/ESP] Cannot find Fallout4.exe module, "
					"sub-hooks unavailable"sv);
			}
			else
			{
				uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hGame);
				REX::INFO("[Profiler/ESP] Fallout4.exe base: {:016X}"sv, moduleBase);

				const uintptr_t constructRVA = isOG
					? kOG_ConstructObjectList_RVA : kNG_ConstructObjectList_RVA;

				if (constructRVA != 0)
				{
					uintptr_t constructAddr = moduleBase + constructRVA;

					auto* p = reinterpret_cast<const uint8_t*>(constructAddr);
					REX::INFO("[Profiler/ESP] ConstructObjectList at {:016X} (RVA {:06X}), "
						"prologue: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}"sv,
						constructAddr, constructRVA,
						p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

					*(uintptr_t*)(&OriginalConstructObjectList) =
						RELEX::DetourJump(constructAddr, (uintptr_t)&HookConstructObjectList);

					if (OriginalConstructObjectList)
					{
						REX::INFO("[Profiler/ESP] ConstructObjectList hooked "
							"(trampoline: {:016X})"sv,
							reinterpret_cast<uintptr_t>(OriginalConstructObjectList));
					}
					else
					{
						REX::WARN("[Profiler/ESP] Failed to hook ConstructObjectList"sv);
					}
				}
				else
				{
					REX::INFO("[Profiler/ESP] ConstructObjectList RVA not configured for "
						"{} runtime"sv, isOG ? "OG"sv : "NG"sv);
				}

				const uintptr_t initRVA = isOG
					? kOG_InitAllForms_RVA : kNG_InitAllForms_RVA;

				if (initRVA != 0)
				{
					uintptr_t initAddr = moduleBase + initRVA;

					auto* p = reinterpret_cast<const uint8_t*>(initAddr);
					REX::INFO("[Profiler/ESP] InitAllForms at {:016X} (RVA {:06X}), "
						"prologue: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}"sv,
						initAddr, initRVA,
						p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

					*(uintptr_t*)(&OriginalInitAllForms) =
						RELEX::DetourJump(initAddr, (uintptr_t)&HookInitAllForms);

					if (OriginalInitAllForms)
					{
						REX::INFO("[Profiler/ESP] InitAllForms hooked "
							"(trampoline: {:016X})"sv,
							reinterpret_cast<uintptr_t>(OriginalInitAllForms));
					}
					else
					{
						REX::WARN("[Profiler/ESP] Failed to hook InitAllForms"sv);
					}
				}
				else
				{
					REX::INFO("[Profiler/ESP] InitAllForms RVA not configured for "
						"{} runtime"sv, isOG ? "OG"sv : "NG"sv);
				}
			}
		}

		m_installed = (OriginalCompileFiles != nullptr);

		REX::INFO("[Profiler/ESP] Installation {} "
			"(CompileFiles: {}, ConstructObjectList: {}, InitAllForms: {})"sv,
			m_installed ? "complete"sv : "FAILED"sv,
			OriginalCompileFiles ? "OK"sv : "FAIL"sv,
			OriginalConstructObjectList ? "OK"sv : "SKIP"sv,
			OriginalInitAllForms ? "OK"sv : "SKIP"sv);
	}
}
