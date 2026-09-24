#pragma once

#include <concepts>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

namespace Addictol
{
	enum class HeapKind
	{
		Stock,
		Voltek,
		Mimalloc,
		Rpmalloc
	};

	// Each backend reports only what it tracks; absent fields export as empty telemetry cells.
	struct HeapStatistics
	{
		std::optional<uint64_t> committedBytes;
		std::optional<uint64_t> reservedBytes;
		std::optional<uint64_t> poolCount;
		std::optional<uint64_t> pagesBusy;
		std::optional<uint64_t> pageCapacity;
		std::optional<uint64_t> liveBlocks;
		std::optional<uint64_t> requestedBytes;
	};

	// Cumulative counters for one size class; telemetry reports their interval deltas.
	struct HeapClassStatistics
	{
		std::string_view label;
		uint64_t allocations{ 0 };
		uint64_t allocatedBytes{ 0 };
		uint64_t pagesCreated{ 0 };
		uint64_t pagesReleased{ 0 };
		uint64_t lockContended{ 0 };
		uint64_t lockWaitTicks{ 0 };
	};

	inline constexpr size_t kMaxHeapClasses{ 64 };

	// Hooks install only after Initialize succeeds; Free, Size and Reallocate must ignore unowned pointers.
	template<class T>
	concept HeapBackend = requires(void* a_block, size_t a_size, size_t a_alignment)
	{
		{ T::kKind } -> std::convertible_to<HeapKind>;
		{ T::kName } -> std::convertible_to<std::string_view>;
		{ T::kTailPadding } -> std::convertible_to<size_t>;
		{ T::Initialize() } noexcept -> std::same_as<bool>;
		{ T::Allocate(a_size) } noexcept -> std::same_as<void*>;
		{ T::AllocateAligned(a_size, a_alignment) } noexcept -> std::same_as<void*>;
		{ T::Reallocate(a_block, a_size) } noexcept -> std::same_as<void*>;
		{ T::ReallocateAligned(a_block, a_size, a_alignment) } noexcept -> std::same_as<void*>;
		{ T::Free(a_block) } noexcept;
		{ T::Size(a_block) } noexcept -> std::same_as<size_t>;
		{ T::Statistics() } noexcept -> std::same_as<HeapStatistics>;
	};

	inline constexpr size_t kNaturalHeapAlignment{ 16 };

	// Havok cloth reads past block ends; header-less backends pad so a block at a commit boundary cannot fault.
	inline constexpr size_t kOverreadPadding{ 16 };
}
