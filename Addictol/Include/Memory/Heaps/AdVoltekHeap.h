#pragma once

#include <Memory/Heaps/AdHeapBackend.h>

namespace Addictol::Heaps
{
	struct Voltek
	{
		static constexpr HeapKind kKind{ HeapKind::Voltek };
		// VMM's per-block headers already keep the bytes past a block readable.
		static constexpr size_t kTailPadding{ 0 };
		static constexpr std::string_view kName{ "voltek" };

		[[nodiscard]] static bool Initialize() noexcept;
		[[nodiscard]] static void* Allocate(size_t a_size) noexcept;
		[[nodiscard]] static void* AllocateAligned(size_t a_size, size_t a_alignment) noexcept;
		[[nodiscard]] static void* Reallocate(void* a_block, size_t a_size) noexcept;
		[[nodiscard]] static void* ReallocateAligned(void* a_block, size_t a_size, size_t a_alignment) noexcept;
		static void Free(void* a_block) noexcept;
		[[nodiscard]] static size_t Size(void* a_block) noexcept;
		[[nodiscard]] static HeapStatistics Statistics() noexcept;
		static void EnableClassStatistics() noexcept;
		[[nodiscard]] static size_t ClassStatistics(std::span<HeapClassStatistics> a_out) noexcept;
	};
}