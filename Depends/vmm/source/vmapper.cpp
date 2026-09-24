// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#include "vmapper.h"
#include "vassert.h"

#if (defined(_WIN32) || defined(_WIN64))
#	include <windows.h>
#endif

#include <intrin.h>

namespace voltek
{
	namespace core
	{
		char* region::reserve(size_t size) noexcept
		{
			if (_size)
				return nullptr;
			auto* base = static_cast<char*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE));
			if (!base)
				return nullptr;
			_base = reinterpret_cast<uintptr_t>(base);
			_size = size;
			return base;
		}

		bool region::commit(void* ptr, size_t size) noexcept
		{
			return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
		}

		void region::decommit(void* ptr, size_t size) noexcept
		{
			VirtualFree(ptr, size, MEM_DECOMMIT);
		}

		void mapper::assign(char* base, size_t slot_size, size_t slot_count)
		{
			_base = base;
			_slot_size = slot_size;
			_slot_count = slot_count;
			_used.assign((slot_count + 63) / 64, 0);
			_committed_pages.assign(slot_count, 0);
			// Bits past the last slot read as used so the scan never hands them out.
			if (const auto tail = slot_count % 64)
				_used.back() = ~0ull << tail;
		}

		void* mapper::allocate(size_t commit_size) noexcept
		{
			if (!commit_size || commit_size > _slot_size)
				return nullptr;

			size_t index = 0;
			{
				_internal::simple_scope_lock scope_lock(_lock);
				size_t word = _hint;
				while (word < _used.size() && _used[word] == ~0ull)
					++word;
				if (word == _used.size())
					return nullptr;
				unsigned long bit = 0;
				_BitScanForward64(&bit, ~_used[word]);
				_used[word] |= 1ull << bit;
				_hint = word;
				++_used_count;
				index = word * 64 + bit;
			}

			auto* slot = _base + index * _slot_size;
			const auto pages = (commit_size + region::commit_granularity - 1) / region::commit_granularity;
			if (region::commit(slot, commit_size))
			{
				_committed_pages[index] = static_cast<uint32_t>(pages);
				_committed.fetch_add(pages * region::commit_granularity, std::memory_order_relaxed);
				return slot;
			}

			release(slot);
			return nullptr;
		}

		void mapper::release(const void* slot) noexcept
		{
			const auto index = static_cast<size_t>(static_cast<const char*>(slot) - _base) / _slot_size;
			// Decommit before the slot becomes reusable, or a new owner's commit could be undone.
			region::decommit(_base + index * _slot_size, _slot_size);
			_committed.fetch_sub(static_cast<uint64_t>(_committed_pages[index]) * region::commit_granularity, std::memory_order_relaxed);
			_committed_pages[index] = 0;

			_internal::simple_scope_lock scope_lock(_lock);
			_used[index / 64] &= ~(1ull << (index % 64));
			--_used_count;
			if (index / 64 < _hint)
				_hint = index / 64;
		}
	}
}