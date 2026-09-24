// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#pragma once

#include "vsimplelock.h"

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace voltek
{
	namespace core
	{
		// One reservation holds every block the manager hands out, so ownership is a range check.
		class region
		{
		public:
			inline static constexpr size_t granularity = 64 * 1024;
			inline static constexpr size_t commit_granularity = 4 * 1024;

			[[nodiscard]] static char* reserve(size_t size) noexcept;
			[[nodiscard]] static bool contains(const void* ptr) noexcept
			{
				return reinterpret_cast<uintptr_t>(ptr) - _base < _size;
			}
			[[nodiscard]] static size_t reserved() noexcept { return _size; }
			[[nodiscard]] static bool commit(void* ptr, size_t size) noexcept;
			static void decommit(void* ptr, size_t size) noexcept;
		private:
			inline static uintptr_t _base{ 0 };
			inline static size_t _size{ 0 };
		};

		// Equal-size slots over a slice of the region; each slot commits only what its user asks for.
		class mapper
		{
		public:
			mapper() = default;
			mapper(const mapper&) = delete;
			mapper& operator=(const mapper&) = delete;

			void assign(char* base, size_t slot_size, size_t slot_count);
			[[nodiscard]] void* allocate(size_t commit_size) noexcept;
			void release(const void* slot) noexcept;
			[[nodiscard]] bool contains(const void* ptr) const noexcept
			{
				return reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(_base) < _slot_size * _slot_count;
			}
			[[nodiscard]] size_t slot_size() const noexcept { return _slot_size; }
			[[nodiscard]] size_t slot_count() const noexcept { return _slot_count; }
			[[nodiscard]] size_t used_count() const noexcept { return _used_count; }
			[[nodiscard]] uint64_t committed_bytes() const noexcept { return _committed.load(std::memory_order_relaxed); }
		private:
			char* _base{ nullptr };
			size_t _slot_size{ 0 };
			size_t _slot_count{ 0 };
			size_t _used_count{ 0 };
			size_t _hint{ 0 };
			std::vector<uint64_t> _used;
			// Committed extent per slot, in commit pages; release decommits and accounts exactly this.
			std::vector<uint32_t> _committed_pages;
			std::atomic<uint64_t> _committed{ 0 };
			_internal::simple_lock _lock;
		};
	}
}
