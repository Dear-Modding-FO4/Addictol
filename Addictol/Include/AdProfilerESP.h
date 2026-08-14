#pragma once

#include <cstdint>

#include <AdProfilerESPSubHooks.h>
#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	class ESPProfiler :
		public REX::Singleton<ESPProfiler>
	{
	private:
		bool m_installed{ false };
		bool m_installAttempted{ false };
		int32_t m_currentFileIndex{ 0 };

		// Address Library IDs: OG 57137, NG/AE 2192321.
		static inline bool(__fastcall* OriginalCompileFiles)(void*, bool) = nullptr;

		static inline ESPSubHooks::ConstructObjectList OriginalConstructObjectList = nullptr;

		static inline ESPSubHooks::InitAllForms OriginalInitAllForms = nullptr;

		ESPProfiler(const ESPProfiler&) = delete;
		ESPProfiler& operator=(const ESPProfiler&) = delete;

		static bool __fastcall HookCompileFiles(void* a_this, bool a_load) noexcept;
		static bool __fastcall HookConstructObjectList(void* a_this, void* a_file, bool a_isFirst) noexcept;
		static void __fastcall HookInitAllForms(void* a_this) noexcept;

		// TESFile::filename is an inline char[260] at +0x70 and requires SEH-protected access.
		[[nodiscard]] static const char* GetTESFileName(void* a_file) noexcept;

	public:
		ESPProfiler() = default;
		virtual ~ESPProfiler() = default;

		// A prior detour attempt makes retries unsafe even when installation failed.
		void Install() noexcept;

		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
