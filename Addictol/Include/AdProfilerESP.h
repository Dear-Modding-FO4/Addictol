#pragma once

#include <cstdint>
#include <vector>

#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	// Profiles ESP/ESM file loading times by hooking TESDataHandler functions.
	// Hooks CompileFiles (total), ConstructObjectList (per-file), and InitAllForms.
	// Results are fed to ProfilerCore for aggregation and reporting.
	class ESPProfiler :
		public REX::Singleton<ESPProfiler>
	{
	public:
		// Describes a resolved CALL (E8 rel32) instruction found during function body scanning
		struct CallSiteInfo
		{
			uintptr_t site{ 0 };      // Address of the E8 opcode byte
			uintptr_t target{ 0 };    // Resolved absolute call target
			std::size_t offset{ 0 };  // Byte offset from the scanned function's entry point
		};

	private:
		bool m_installed{ false };
		std::int32_t m_currentFileIndex{ 0 };

		// --- Original function pointers (set by Detours during Install) ---

		// TESDataHandler::CompileFiles
		// Loads all ESP/ESM files in the load order. Wraps the entire compilation phase.
		// Address Library ids: OG 57137, NG/AE 2192321.
		// bool __fastcall CompileFiles(TESDataHandler* this, bool load)
		static inline bool(__fastcall* OriginalCompileFiles)(void*, bool) = nullptr;

		// TESDataHandler::ConstructObjectList
		// Per-file: reads all records from a single TESFile. The heaviest per-file phase.
		// No address library ID — resolved via known RVA from Ghidra/F4LoadTimeProfiler.
		// void __fastcall ConstructObjectList(TESDataHandler*, TESFile*, bool, void*)
		static inline void(__fastcall* OriginalConstructObjectList)(void*, void*, bool, void*) = nullptr;

		// TESDataHandler::InitAllForms
		// Post-load: resolves cross-references and initializes all loaded forms.
		// No address library ID — resolved via known RVA from Ghidra/F4LoadTimeProfiler.
		// void __fastcall InitAllForms(TESDataHandler*)
		static inline void(__fastcall* OriginalInitAllForms)(void*) = nullptr;

		ESPProfiler(const ESPProfiler&) = delete;
		ESPProfiler& operator=(const ESPProfiler&) = delete;

		// --- Hook implementations ---
		static bool __fastcall HookCompileFiles(void* a_this, bool a_load) noexcept;
		static void __fastcall HookConstructObjectList(void* a_this, void* a_file, bool a_isFirst, void* a_param4) noexcept;
		static void __fastcall HookInitAllForms(void* a_this) noexcept;

		// --- Utilities ---

		// Extracts the filename from a TESFile* using SEH-protected memory access.
		// TESFile::filename is an inline char[260] at offset +0x70.
		[[nodiscard]] static const char* GetTESFileName(void* a_file) noexcept;

		// Scans a function body for near CALL (E8 rel32) instructions.
		// Returns all resolved call sites within the scan window.
		[[nodiscard]] static std::vector<CallSiteInfo> ScanCallSites(
			uintptr_t a_funcBase, std::size_t a_maxBytes) noexcept;

	public:
		ESPProfiler() = default;
		virtual ~ESPProfiler() = default;

		// Installs all ESP profiling hooks. Requires ProfilerCore to be active.
		// Safe to call multiple times (no-op after first successful install).
		void Install() noexcept;

		// Returns true if hooks were successfully installed.
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
