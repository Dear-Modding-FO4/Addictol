#include <Memory/Heaps/AdRpmallocHeap.h>
#include <rpmalloc.h>

#include <array>
#include <atomic>
#include <windows.h>

namespace Addictol::Heaps
{
	namespace
	{
		// rpmalloc resolves a block's span by masking to this size; it must match SPAN_SIZE.
		inline constexpr uintptr_t kGranuleShift{ 28 };
		inline constexpr uintptr_t kGranuleSize{ uintptr_t{ 1 } << kGranuleShift };
		inline constexpr uintptr_t kAddressShift{ 47 };
		inline constexpr size_t kGranuleCount{ size_t{ 1 } << (kAddressShift - kGranuleShift) };

		// One bit per 256 MiB granule; set only for granules wholly inside an rpmalloc mapping.
		std::array<std::atomic<uint64_t>, kGranuleCount / 64> s_ownedGranules{};
		std::atomic<uint64_t> s_reservedBytes{ 0 };

		using VirtualAlloc2Function = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
		VirtualAlloc2Function s_virtualAlloc2{ nullptr };

		[[nodiscard]] constexpr uintptr_t RoundUp(uintptr_t a_value, uintptr_t a_alignment) noexcept
		{
			return (a_value + a_alignment - 1) & ~(a_alignment - 1);
		}

		void MarkGranules(uintptr_t a_regionBegin, uintptr_t a_regionEnd, bool a_owned) noexcept
		{
			for (auto granule = RoundUp(a_regionBegin, kGranuleSize); granule + kGranuleSize <= a_regionEnd; granule += kGranuleSize)
			{
				const auto index = granule >> kGranuleShift;
				const auto bit = uint64_t{ 1 } << (index & 63);
				auto& word = s_ownedGranules[index >> 6];
				if (a_owned)
					word.fetch_or(bit, std::memory_order_relaxed);
				else
					word.fetch_and(~bit, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool Owns(const void* a_block) noexcept
		{
			const auto address = reinterpret_cast<uintptr_t>(a_block);
			if (!address || (address >> kAddressShift))
				return false;
			const auto index = address >> kGranuleShift;
			return (s_ownedGranules[index >> 6].load(std::memory_order_relaxed) >> (index & 63)) & 1;
		}

		void* Map(size_t a_size, size_t a_alignment, size_t* a_offset, size_t* a_mappedSize)
		{
			// Span mappings cover whole granules so no foreign allocation can share one.
			auto size = a_alignment >= kGranuleSize ? RoundUp(a_size, kGranuleSize) : a_size;
			void* region{ nullptr };
			void* block{ nullptr };
			if (a_alignment && s_virtualAlloc2)
			{
				MEM_ADDRESS_REQUIREMENTS requirements{};
				requirements.Alignment = a_alignment;
				MEM_EXTENDED_PARAMETER parameter{};
				parameter.Type = MemExtendedParameterAddressRequirements;
				parameter.Pointer = &requirements;
				region = block = s_virtualAlloc2(nullptr, nullptr, size, MEM_RESERVE, PAGE_READWRITE, &parameter, 1);
			}
			else
			{
				size += a_alignment;
				region = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
				if (region)
					block = reinterpret_cast<void*>(RoundUp(reinterpret_cast<uintptr_t>(region), a_alignment ? a_alignment : 1));
			}
			if (!block)
				return nullptr;
			*a_offset = reinterpret_cast<uintptr_t>(block) - reinterpret_cast<uintptr_t>(region);
			*a_mappedSize = size;
			MarkGranules(reinterpret_cast<uintptr_t>(region), reinterpret_cast<uintptr_t>(region) + size, true);
			s_reservedBytes.fetch_add(size, std::memory_order_relaxed);
			return block;
		}

		int Commit(void* a_address, size_t a_size)
		{
			return VirtualAlloc(a_address, a_size, MEM_COMMIT, PAGE_READWRITE) ? 0 : 1;
		}

		int Decommit(void* a_address, size_t a_size)
		{
			return VirtualFree(a_address, a_size, MEM_DECOMMIT) ? 0 : 1;
		}

		void Unmap(void* a_address, size_t a_offset, size_t a_mappedSize)
		{
			const auto region = reinterpret_cast<uintptr_t>(a_address) - a_offset;
			MarkGranules(region, region + a_mappedSize, false);
			s_reservedBytes.fetch_sub(a_mappedSize, std::memory_order_relaxed);
			VirtualFree(reinterpret_cast<void*>(region), 0, MEM_RELEASE);
		}

		rpmalloc_interface_t s_interface{ &Map, &Commit, &Decommit, &Unmap, nullptr, nullptr };
	}

	void Rpmalloc::Initialize() noexcept
	{
		if (const auto kernel = GetModuleHandleW(L"kernelbase.dll"))
			s_virtualAlloc2 = reinterpret_cast<VirtualAlloc2Function>(GetProcAddress(kernel, "VirtualAlloc2"));
		rpmalloc_initialize(&s_interface);
	}

	void* Rpmalloc::Allocate(size_t a_size) noexcept
	{
		return rpmalloc(a_size);
	}

	void* Rpmalloc::AllocateAligned(size_t a_size, size_t a_alignment) noexcept
	{
		return a_alignment > kNaturalHeapAlignment ?
			rpaligned_alloc(a_alignment, a_size) :
			rpmalloc(a_size);
	}

	void* Rpmalloc::Reallocate(void* a_block, size_t a_size) noexcept
	{
		return ReallocateAligned(a_block, a_size, kNaturalHeapAlignment);
	}

	void* Rpmalloc::ReallocateAligned(void* a_block, size_t a_size, size_t a_alignment) noexcept
	{
		if (!Owns(a_block))
			return nullptr;
		return a_alignment > kNaturalHeapAlignment ?
			rpaligned_realloc(a_block, a_alignment, a_size, rpmalloc_usable_size(a_block), 0) :
			rprealloc(a_block, a_size);
	}

	void Rpmalloc::Free(void* a_block) noexcept
	{
		if (Owns(a_block))
			rpfree(a_block);
	}

	size_t Rpmalloc::Size(void* a_block) noexcept
	{
		return Owns(a_block) ? rpmalloc_usable_size(a_block) : 0;
	}

	HeapStatistics Rpmalloc::Statistics() noexcept
	{
		HeapStatistics result{};
		result.reservedBytes = s_reservedBytes.load(std::memory_order_relaxed);
		return result;
	}
}
