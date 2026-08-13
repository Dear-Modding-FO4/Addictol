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

	// -----------------------------------------------------------------
	// SEH-isolated helpers
	// These must NOT contain local C++ objects with non-trivial destructors
	// (MSVC C2712: Cannot use __try in functions that require object unwinding)
	// -----------------------------------------------------------------

	// Calls the original CompileFiles trampoline under SEH protection.
	// Returns: 0 = exception caught, 1 = original returned true, 2 = original returned false
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

	// Calls the original ConstructObjectList trampoline under SEH protection.
	// Returns false if an exception was caught.
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

	// Calls the original InitAllForms trampoline under SEH protection.
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

	// Scans memory starting at a_funcBase for E8 (near relative CALL) instructions.
	// Writes resolved CallSiteInfo entries into a_outBuf (up to a_maxResults).
	// Returns the number of valid call sites found.
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

				// Resolve relative CALL: target = (call_addr + 5) + int32_displacement
				auto disp = *reinterpret_cast<const std::int32_t*>(a_funcBase + i + 1);
				uintptr_t site = a_funcBase + i;
				uintptr_t target = site + 5 + static_cast<std::intptr_t>(disp);

				// Validate: target should be within ±1 GB of call site and above low memory.
				// This filters out false positives where 0xE8 appears as part of an immediate
				// operand in another instruction rather than as a CALL opcode.
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
	// -----------------------------------------------------------------
	// TOML configuration
	// -----------------------------------------------------------------
	static REX::TOML::F64<> fWarnThresholdMs{ "Profiler"sv, "fWarnThresholdMs"sv, 500.0 };
	static REX::TOML::F64<> fCritThresholdMs{ "Profiler"sv, "fCritThresholdMs"sv, 2000.0 };
	static REX::TOML::Bool<> bESPSubHooks{ "Profiler"sv, "bESPSubHooks"sv, false };

	// -----------------------------------------------------------------
	// Legacy sub-hook RVAs
	// -----------------------------------------------------------------
	// These are offsets from the Fallout4.exe module base, confirmed by
	// Ghidra decompilation, F4LoadTimeProfiler, and NG PDB analysis.
	//
	// OG (1.10.163):
	//   ConstructObjectList: 0x118750 (594 bytes, 4 params: this, TESFile*, bool, void*)
	//   InitAllForms:        0x11B070 (2116 bytes, 1 param: this)
	//
	// NG (1.11.191) — extracted from Fallout41.11.191.0.pdb public symbols:
	//   ConstructObjectList: 0x2DFA40 (NOT in PDB publics; confirmed by F4LoadTimeProfiler)
	//   InitAllForms:        0x2EC830 (PDB: ?InitAllForms@TESDataHandler@@QEAAXXZ,
	//                                  Sec=1, SecOff=0x2EB830, .text VA=0x1000)
	//   NOTE: Prior value 0x2EB570 was SetMasterFileLargeBuffer (wrong function!)
	//
	// NG pipeline changes (1.11.191):
	//   CompileFiles completes in ~50ms (vs ~10,000ms on OG) — now a lightweight
	//   setup/metadata phase. The actual per-file loading was moved outside
	//   CompileFiles into a deferred/restructured path.
	//
	//   ConstructObjectList was massively refactored (~594 bytes -> ~13,600 bytes).
	//   On NG it fires only ONCE during CompileFiles with a null/placeholder file,
	//   not once-per-file as on OG. The per-file loop was eliminated or moved.
	//
	//   CheckModsLoaded also grew from ~115 to ~3,984 bytes, suggesting the
	//   loading orchestration was redistributed across multiple functions.
	//
	//   The ~3.5s between CompileFiles_End and GameDataReady corresponds to
	//   form loading that previously happened inside CompileFiles on OG.
	static constexpr uintptr_t kOG_ConstructObjectList_RVA = 0x118750;
	static constexpr uintptr_t kOG_InitAllForms_RVA        = 0x11B070;
	static constexpr uintptr_t kNG_ConstructObjectList_RVA  = 0x2DFA40;
	static constexpr uintptr_t kNG_InitAllForms_RVA         = 0x2EC830; // PDB-confirmed

	// NG-only: ConstructObject (per-form, not per-file) — potential per-form hook target
	// Signature: bool ConstructObject(TESFile*, bool, TESForm*, bool)
	// PDB: ?ConstructObject@TESDataHandler@@QEAA_NPEAVTESFile@@_NPEAVTESForm@@1@Z
	static constexpr uintptr_t kNG_ConstructObject_RVA      = 0x2EA240;

	// TESFile::filename — inline char[260] at this offset from TESFile*
	static constexpr uintptr_t kTESFileNameOffset = 0x70;

	// -----------------------------------------------------------------
	// Call-site scanning
	// -----------------------------------------------------------------

	std::vector<ESPProfiler::CallSiteInfo> ESPProfiler::ScanCallSites(
		uintptr_t a_funcBase, std::size_t a_maxBytes) noexcept
	{
		// Stack buffer sized for the maximum plausible number of CALLs in 4 KB of code.
		// 256 * sizeof(CallSiteInfo) ≈ 6 KB — acceptable for a one-time init call.
		static constexpr std::size_t kMaxResults = 256;
		CallSiteInfo buffer[kMaxResults]{};

		auto count = ScanCallSitesImpl(a_funcBase, a_maxBytes, buffer, kMaxResults);
		return { buffer, buffer + count };
	}

	// -----------------------------------------------------------------
	// TESFile filename extraction
	// -----------------------------------------------------------------

	const char* ESPProfiler::GetTESFileName(void* a_file) noexcept
	{
		if (!a_file)
			return nullptr;

		// No C++ objects here — __try is safe.
		__try
		{
			auto addr = reinterpret_cast<uintptr_t>(a_file) + kTESFileNameOffset;
			const char* name = reinterpret_cast<const char*>(addr);

			// Sanity: first character must be printable ASCII
			if (name[0] > 0x1F && name[0] < 0x7F)
				return name;
		}
		__except (1)
		{
		}

		return nullptr;
	}

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::CompileFiles
	// Wraps the entire plugin compilation pass and records total time.
	// -----------------------------------------------------------------

	bool __fastcall ESPProfiler::HookCompileFiles(void* a_this, bool a_load) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		// Passthrough if profiler became inactive or original was not captured
		if (!core->IsActive() || !OriginalCompileFiles)
			return OriginalCompileFiles ? OriginalCompileFiles(a_this, a_load) : false;

		REX::INFO("[Profiler/ESP] CompileFiles entered (this={:016X})"sv,
			reinterpret_cast<uintptr_t>(a_this));

		// Reset the per-file counter for this compilation pass
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

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::ConstructObjectList
	// Times per-file record loading and feeds ESPProfileEntry to core.
	// -----------------------------------------------------------------

	void __fastcall ESPProfiler::HookConstructObjectList(void* a_this, void* a_file, bool a_isFirst, void* a_param4) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();
		auto* self = GetSingleton();

		// Passthrough if profiler is inactive
		if (!core->IsActive() || !OriginalConstructObjectList)
		{
			if (OriginalConstructObjectList)
				OriginalConstructObjectList(a_this, a_file, a_isFirst, a_param4);
			return;
		}

		ESPProfileEntry entry;
		entry.loadOrderIndex = self->m_currentFileIndex++;

		// Extract filename from TESFile* at offset + 0x70 (SEH-protected)
		const char* name = GetTESFileName(a_file);
		entry.filename = name ? name : "(unknown)"sv;

		// Time the record construction phase (the heaviest per-file operation)
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

		// Threshold-based warnings for slow-loading files
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

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::InitAllForms
	// Times the post-load form initialization pass.
	// -----------------------------------------------------------------

	void __fastcall ESPProfiler::HookInitAllForms(void* a_this) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		// Passthrough if profiler is inactive
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

	// -----------------------------------------------------------------
	// Installation
	// -----------------------------------------------------------------

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

		// ---- Step 1: Locate CompileFiles ----

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

		// ---- Step 2: Hook CompileFiles ----

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

		// ---- Step 3: Diagnostic call-site scan ----
		// Dumps all E8 CALL sites in CompileFiles for version identification.
		// This is informational only — hooking uses direct RVA + DetourJump,
		// NOT call-site patching. Useful for identifying functions in new builds.

		static constexpr std::size_t kMaxScanBytes = 0x1000;
		auto callSites = ScanCallSites(compileFilesAddr, kMaxScanBytes);

		REX::INFO("[Profiler/ESP] Diagnostic: {} call sites in CompileFiles ({} bytes):"sv,
			callSites.size(), kMaxScanBytes);

		for (std::size_t i = 0; i < callSites.size(); ++i)
		{
			REX::INFO("[Profiler/ESP] [{:2d}] site={:016X} target={:016X} (offset + 0x{:04X})"sv,
				i, callSites[i].site, callSites[i].target, callSites[i].offset);
		}

		// ---- Step 4: Hook ConstructObjectList + InitAllForms via known RVAs ----
		// These functions have no address library IDs. We resolve them using
		// hardcoded RVAs confirmed by Ghidra analysis and F4LoadTimeProfiler.
		// Uses DetourJump (inline hook at function entry), NOT call-site patching.

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

				// ---- ConstructObjectList (4 params: this, TESFile*, bool, void*) ----
				const uintptr_t constructRVA = isOG
					? kOG_ConstructObjectList_RVA : kNG_ConstructObjectList_RVA;

				if (constructRVA != 0)
				{
					uintptr_t constructAddr = moduleBase + constructRVA;

					// Log prologue bytes for verification
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

				// ---- InitAllForms (1 param: this) ----
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

		// ---- Done ----

		m_installed = (OriginalCompileFiles != nullptr);

		REX::INFO("[Profiler/ESP] Installation {} "
			"(CompileFiles: {}, ConstructObjectList: {}, InitAllForms: {})"sv,
			m_installed ? "complete"sv : "FAILED"sv,
			OriginalCompileFiles ? "OK"sv : "FAIL"sv,
			OriginalConstructObjectList ? "OK"sv : "SKIP"sv,
			OriginalInitAllForms ? "OK"sv : "SKIP"sv);
	}
}
