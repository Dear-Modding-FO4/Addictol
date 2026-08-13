#pragma once

#include <cstdint>
#include <vector>

#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	class ESPProfiler :
		public REX::Singleton<ESPProfiler>
	{
	public:
		struct CallSiteInfo
		{
			uintptr_t site{ 0 };      // Address of the E8 opcode byte
			uintptr_t target{ 0 };    // Resolved absolute call target
			std::size_t offset{ 0 };  // Byte offset from the scanned function's entry point
		};

	private:
		bool m_installed{ false };
		std::int32_t m_currentFileIndex{ 0 };

		// Address Library IDs: OG 57137, NG/AE 2192321.
		static inline bool(__fastcall* OriginalCompileFiles)(void*, bool) = nullptr;

		// No Address Library ID; resolved from a known Ghidra/F4LoadTimeProfiler RVA.
		static inline void(__fastcall* OriginalConstructObjectList)(void*, void*, bool, void*) = nullptr;

		// No Address Library ID; resolved from a known Ghidra/F4LoadTimeProfiler RVA.
		static inline void(__fastcall* OriginalInitAllForms)(void*) = nullptr;

		ESPProfiler(const ESPProfiler&) = delete;
		ESPProfiler& operator=(const ESPProfiler&) = delete;

		static bool __fastcall HookCompileFiles(void* a_this, bool a_load) noexcept;
		static void __fastcall HookConstructObjectList(void* a_this, void* a_file, bool a_isFirst, void* a_param4) noexcept;
		static void __fastcall HookInitAllForms(void* a_this) noexcept;

		// TESFile::filename is an inline char[260] at +0x70 and requires SEH-protected access.
		[[nodiscard]] static const char* GetTESFileName(void* a_file) noexcept;

		[[nodiscard]] static std::vector<CallSiteInfo> ScanCallSites(
			uintptr_t a_funcBase, std::size_t a_maxBytes) noexcept;

	public:
		ESPProfiler() = default;
		virtual ~ESPProfiler() = default;

		// No-op while ProfilerCore is inactive or after successful installation.
		void Install() noexcept;

		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
