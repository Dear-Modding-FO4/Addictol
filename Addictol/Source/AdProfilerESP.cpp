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

	int SafeCallConstructObjectList(
		ESPSubHooks::ConstructObjectList a_original,
		void* a_this,
		void* a_file,
		bool a_isFirst) noexcept
	{
		__try
		{
			return a_original(a_this, a_file, a_isFirst) ? 1 : 2;
		}
		__except (1)
		{
			return 0;
		}
	}

	bool SafeCallInitAllForms(ESPSubHooks::InitAllForms a_original, void* a_this) noexcept
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

}

namespace Addictol
{
	static REX::TOML::F64<> fWarnThresholdMs{ "Profiler"sv, "fWarnThresholdMs"sv, 500.0 };
	static REX::TOML::F64<> fCritThresholdMs{ "Profiler"sv, "fCritThresholdMs"sv, 2000.0 };
	static REX::TOML::Bool<> bESPSubHooks{ "Profiler"sv, "bESPSubHooks"sv, false };

	// ConstructObjectList remains a per-file Boolean pass on every supported runtime.

	// TESFile::filename is an inline char[260] at +0x70.
	static constexpr uintptr_t kTESFileNameOffset = 0x70;

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

	bool __fastcall ESPProfiler::HookConstructObjectList(
		void* a_this,
		void* a_file,
		bool a_isFirst) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();
		auto* self = GetSingleton();

		if (!core->IsActive() || !OriginalConstructObjectList)
			return OriginalConstructObjectList ?
				OriginalConstructObjectList(a_this, a_file, a_isFirst) :
				false;

		ESPProfileEntry entry;
		entry.loadOrderIndex = self->m_currentFileIndex++;

		const char* name = GetTESFileName(a_file);
		entry.filename = name ? name : "(unknown)"sv;

		auto start = std::chrono::high_resolution_clock::now();
		const auto callResult = SafeCallConstructObjectList(
			OriginalConstructObjectList, a_this, a_file, a_isFirst);
		auto end = std::chrono::high_resolution_clock::now();
		entry.constructMs = std::chrono::duration<double, std::milli>(end - start).count();

		if (callResult == 0)
		{
			REX::ERROR("[Profiler/ESP] ConstructObjectList CRASHED on file [{}] {}!"sv,
				entry.loadOrderIndex, entry.filename);
			return false;
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
		return callResult == 1;
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
		if (m_installAttempted)
		{
			REX::ERROR("[Profiler/ESP] Refusing unsafe retry after a prior detour attempt."sv);
			return;
		}

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
		const auto installSubHooks = bESPSubHooks.GetValue();
		const auto& constructTarget = ESPSubHooks::GetConstructTarget(runtime);
		const auto& initTarget = ESPSubHooks::GetInitTarget(runtime);
		const auto constructAddr = installSubHooks ? REL::ID{
			ESPSubHooks::GetConstructTarget(ESPCompileFiles::Runtime::OG).id,
			ESPSubHooks::GetConstructTarget(ESPCompileFiles::Runtime::NG).id,
			ESPSubHooks::GetConstructTarget(ESPCompileFiles::Runtime::AE).id
		}.address() : 0;
		const auto initAddr = installSubHooks ? REL::ID{
			ESPSubHooks::GetInitTarget(ESPCompileFiles::Runtime::OG).id,
			ESPSubHooks::GetInitTarget(ESPCompileFiles::Runtime::NG).id,
			ESPSubHooks::GetInitTarget(ESPCompileFiles::Runtime::AE).id
		}.address() : 0;

		const auto code = std::span{
			reinterpret_cast<const uint8_t*>(compileFilesAddr),
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

		if (installSubHooks)
		{
			REX::INFO(
				"[Profiler/ESP] ConstructObjectList target: runtime {}, slot {}, id {}, address {:016X}."sv,
				detectedRuntime,
				constructTarget.slot,
				constructTarget.id,
				constructAddr);
			const auto constructCode = std::span{
				reinterpret_cast<const uint8_t*>(constructAddr),
				constructTarget.signatureSize
			};
			if (!ESPSubHooks::Matches(constructCode, constructTarget))
			{
				REX::ERROR(
					"[Profiler/ESP] ConstructObjectList exact signature mismatch: runtime {}, slot {}, id {}, address {:016X}; installing nothing."sv,
					detectedRuntime,
					constructTarget.slot,
					constructTarget.id,
					constructAddr);
				return;
			}

			REX::INFO(
				"[Profiler/ESP] InitAllForms target: runtime {}, slot {}, id {}, address {:016X}."sv,
				detectedRuntime,
				initTarget.slot,
				initTarget.id,
				initAddr);
			const auto initCode = std::span{
				reinterpret_cast<const uint8_t*>(initAddr),
				initTarget.signatureSize
			};
			if (!ESPSubHooks::Matches(initCode, initTarget))
			{
				REX::ERROR(
					"[Profiler/ESP] InitAllForms exact signature mismatch: runtime {}, slot {}, id {}, address {:016X}; installing nothing."sv,
					detectedRuntime,
					initTarget.slot,
					initTarget.id,
					initAddr);
				return;
			}
		}

		m_installAttempted = true;

		if (installSubHooks)
		{
			OriginalConstructObjectList = reinterpret_cast<ESPSubHooks::ConstructObjectList>(
				RELEX::DetourJump(constructAddr, reinterpret_cast<uintptr_t>(&HookConstructObjectList)));
			if (!OriginalConstructObjectList)
			{
				REX::ERROR(
					"[Profiler/ESP] ConstructObjectList detour failed after exact validation; target may be left in an indeterminate state. CompileFiles was not armed."sv);
				return;
			}
			REX::INFO("[Profiler/ESP] ConstructObjectList hooked (trampoline: {:016X})"sv,
				reinterpret_cast<uintptr_t>(OriginalConstructObjectList));

			OriginalInitAllForms = reinterpret_cast<ESPSubHooks::InitAllForms>(
				RELEX::DetourJump(initAddr, reinterpret_cast<uintptr_t>(&HookInitAllForms)));
			if (!OriginalInitAllForms)
			{
				REX::ERROR(
					"[Profiler/ESP] InitAllForms detour failed after exact validation; target may be left in an indeterminate state and ConstructObjectList remains installed. CompileFiles was not armed."sv);
				return;
			}
			REX::INFO("[Profiler/ESP] InitAllForms hooked (trampoline: {:016X})"sv,
				reinterpret_cast<uintptr_t>(OriginalInitAllForms));
		}
		else
			REX::INFO("[Profiler/ESP] Sub-hooks disabled (bESPSubHooks=false)."sv);

		OriginalCompileFiles = reinterpret_cast<decltype(OriginalCompileFiles)>(
			RELEX::DetourJump(compileFilesAddr, reinterpret_cast<uintptr_t>(&HookCompileFiles)));
		if (!OriginalCompileFiles)
		{
			REX::ERROR(
				"[Profiler/ESP] CompileFiles detour failed after exact validation; target may be left in an indeterminate state{}."sv,
				installSubHooks ? " and both sub-hooks remain installed"sv : ""sv);
			return;
		}

		REX::INFO("[Profiler/ESP] CompileFiles hooked (trampoline: {:016X})"sv,
			reinterpret_cast<uintptr_t>(OriginalCompileFiles));
		m_installed = true;
		REX::INFO(
			"[Profiler/ESP] Installation complete (CompileFiles: OK, ConstructObjectList: {}, InitAllForms: {})."sv,
			installSubHooks ? "OK"sv : "SKIP"sv,
			installSubHooks ? "OK"sv : "SKIP"sv);
	}
}
