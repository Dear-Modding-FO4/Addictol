#include "AllocationProbe.h"

#include <cstdlib>
#include <malloc.h>
#include <new>

namespace
{
	thread_local bool s_countAllocations{ false };
	thread_local size_t s_allocations{ 0 };

	void* Allocate(size_t a_size, size_t a_alignment = 0)
	{
		if (s_countAllocations)
			++s_allocations;
		const auto size = a_size ? a_size : 1;
		for (;;)
		{
			auto* pointer = a_alignment ? _aligned_malloc(size, a_alignment) : std::malloc(size);
			if (pointer)
				return pointer;
			if (auto handler = std::get_new_handler())
				handler();
			else
				throw std::bad_alloc{};
		}
	}
}

void* operator new(size_t a_size) { return Allocate(a_size); }
void* operator new[](size_t a_size) { return Allocate(a_size); }
void* operator new(size_t a_size, std::align_val_t a_alignment) { return Allocate(a_size, static_cast<size_t>(a_alignment)); }
void* operator new[](size_t a_size, std::align_val_t a_alignment) { return Allocate(a_size, static_cast<size_t>(a_alignment)); }
void operator delete(void* a_pointer) noexcept { std::free(a_pointer); }
void operator delete[](void* a_pointer) noexcept { std::free(a_pointer); }
void operator delete(void* a_pointer, size_t) noexcept { std::free(a_pointer); }
void operator delete[](void* a_pointer, size_t) noexcept { std::free(a_pointer); }
void operator delete(void* a_pointer, std::align_val_t) noexcept { _aligned_free(a_pointer); }
void operator delete[](void* a_pointer, std::align_val_t) noexcept { _aligned_free(a_pointer); }
void operator delete(void* a_pointer, size_t, std::align_val_t) noexcept { _aligned_free(a_pointer); }
void operator delete[](void* a_pointer, size_t, std::align_val_t) noexcept { _aligned_free(a_pointer); }

namespace vmm_tests
{
	AllocationProbe::AllocationProbe() noexcept :
		m_initial(s_allocations), m_previous(s_countAllocations)
	{
		s_countAllocations = true;
	}

	AllocationProbe::~AllocationProbe() noexcept
	{
		s_countAllocations = m_previous;
	}

	size_t AllocationProbe::Count() const noexcept
	{
		return s_allocations - m_initial;
	}
}
