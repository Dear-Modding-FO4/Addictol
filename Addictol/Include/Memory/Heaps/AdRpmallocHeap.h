#pragma once

#include <Memory/Heaps/AdHeapBackend.h>

namespace Addictol::Heaps
{
	struct Rpmalloc
	{
		static constexpr HeapKind kKind{ HeapKind::Rpmalloc };
		static constexpr size_t kTailPadding{ kOverreadPadding };
		static constexpr std::string_view kName{ "rpmalloc" };

		[[nodiscard]] static bool Initialize() noexcept;
		[[nodiscard]] static void* Allocate(size_t a_size) noexcept;
		[[nodiscard]] static void* AllocateAligned(size_t a_size, size_t a_alignment) noexcept;
		[[nodiscard]] static void* Reallocate(void* a_block, size_t a_size) noexcept;
		[[nodiscard]] static void* ReallocateAligned(void* a_block, size_t a_size, size_t a_alignment) noexcept;
		static void Free(void* a_block) noexcept;
		[[nodiscard]] static size_t Size(void* a_block) noexcept;
		[[nodiscard]] static HeapStatistics Statistics() noexcept;
	};
}