#pragma once

#include <REX/REX.h>
#include <Windows.h>
#ifdef ERROR
#	undef ERROR
#endif
#include <cstdint>
#include <span>

namespace RELEX
{
	struct ImportSlot
	{
		void** address{};
		const char* library{};
		const char* name{};
	};

	using ImportVisitor = bool (*)(const ImportSlot&, void*) noexcept;
	[[nodiscard]] bool VisitImports(uintptr_t a_base, ImportVisitor a_visitor, void* a_context) noexcept;

	struct PointerPatch
	{
		void** address{};
		void* target{};
		void* previous{};
		DWORD protection{};
	};

	[[nodiscard]] bool PreparePointers(std::span<PointerPatch> a_patches) noexcept;
	void RestorePointersProtection(std::span<PointerPatch> a_patches) noexcept;
	void PublishPointers(std::span<PointerPatch> a_patches) noexcept;
	[[nodiscard]] uintptr_t PatchImport(uintptr_t a_base, const char* a_library,
		const char* a_name, uintptr_t a_target) noexcept;
}
