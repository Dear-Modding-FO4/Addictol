// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#pragma warning(disable : 6333)
#pragma warning(disable : 26819)
#pragma warning(disable : 28160)

#include "vmapper.h"
#include "vmmmain.h"
#include "vmmpool.h"

#include <atomic>
#include <bit>

#include <limits.h>
#include <string.h>


namespace voltek
{
	namespace core
	{
		// Инициализация систем.
		void initialize();
	}

	namespace memory_manager
	{
		memory_manager* global_memory_manager = nullptr;

		// Detailed class statistics cost a few nanoseconds per call, so they run only while telemetry reads them.
		std::atomic<bool> statistics_enabled{ false };

		typedef page_t<block8_t> page8_t;
		typedef page_t<block16_t> page16_t;
		typedef page_t<block32_t> page32_t;
		typedef page_t<block64_t> page64_t;
		typedef page_t<block128_t> page128_t;
		typedef page_t<block256_t> page256_t;
		typedef page_t<block512_t> page512_t;
		typedef page_t<block1024_t> page1024_t;
		typedef page_t<block4096_t> page4096_t;
		typedef page_t<block8192_t> page8192_t;
		typedef page_t<block16384_t> page16384_t;
		typedef page_t<block32768_t> page32768_t;
		typedef page_t<block65536_t> page65536_t;
		typedef page_t<block131072_t> page131072_t;
		typedef pool_t<block8_t, page8_t> pool8_t;
		typedef pool_t<block16_t, page16_t> pool16_t;
		typedef pool_t<block32_t, page32_t> pool32_t;
		typedef pool_t<block64_t, page64_t> pool64_t;
		typedef pool_t<block128_t, page128_t> pool128_t;
		typedef pool_t<block256_t, page256_t> pool256_t;
		typedef pool_t<block512_t, page512_t> pool512_t;
		typedef pool_t<block1024_t, page1024_t> pool1024_t;
		typedef pool_t<block4096_t, page4096_t> pool4096_t;
		typedef pool_t<block8192_t, page8192_t> pool8192_t;
		typedef pool_t<block16384_t, page16384_t> pool16384_t;
		typedef pool_t<block32768_t, page32768_t> pool32768_t;
		typedef pool_t<block65536_t, page65536_t> pool65536_t;
		typedef pool_t<block131072_t, page131072_t> pool131072_t;

		inline constexpr size_t pool_count = std::to_underlying(pool_type::MAX);

		// Largest request each pool class serves; a larger realloc moves the block.
		inline constexpr std::array<size_t, pool_count> pool_limits{
			8, 16, 32, 64, 128, 256, 512, 1024, 4096, 8192, 16384, 32768, 65536, 131072
		};
		inline constexpr size_t pool_limit_maximum = pool_limits.back();

		[[nodiscard]] constexpr pool_type pool_class_of(size_t size) noexcept
		{
			if (size <= 8)
				return pool_type::pool_8;
			if (size <= 1024)
				return static_cast<pool_type>(std::bit_width(size - 1) - 3);
			if (size <= 4096)
				return pool_type::pool_4096;
			return static_cast<pool_type>(std::bit_width(size - 1) - 4);
		}

		static_assert(pool_class_of(8) == pool_type::pool_8 && pool_class_of(9) == pool_type::pool_16);
		static_assert(pool_class_of(1024) == pool_type::pool_1024 && pool_class_of(1025) == pool_type::pool_4096);
		static_assert(pool_class_of(4097) == pool_type::pool_8192 && pool_class_of(131072) == pool_type::pool_131072);

		// One dispatch from a runtime class to its pool type, shared by every per-class operation.
		template<typename F>
		static decltype(auto) visit_pool(pool_type id, F&& f)
		{
			switch (id)
			{
			case pool_type::pool_8: return f.template operator()<pool8_t>();
			case pool_type::pool_16: return f.template operator()<pool16_t>();
			case pool_type::pool_32: return f.template operator()<pool32_t>();
			case pool_type::pool_64: return f.template operator()<pool64_t>();
			case pool_type::pool_128: return f.template operator()<pool128_t>();
			case pool_type::pool_256: return f.template operator()<pool256_t>();
			case pool_type::pool_512: return f.template operator()<pool512_t>();
			case pool_type::pool_1024: return f.template operator()<pool1024_t>();
			case pool_type::pool_4096: return f.template operator()<pool4096_t>();
			case pool_type::pool_8192: return f.template operator()<pool8192_t>();
			case pool_type::pool_16384: return f.template operator()<pool16384_t>();
			case pool_type::pool_32768: return f.template operator()<pool32768_t>();
			case pool_type::pool_65536: return f.template operator()<pool65536_t>();
			default: return f.template operator()<pool131072_t>();
			}
		}

		template<typename Pool>
		[[nodiscard]] static Pool* loaded_pool(void* const* pools, pool_type id) noexcept
		{
			return reinterpret_cast<Pool*>(std::atomic_ref<void*>(const_cast<void*&>(pools[std::to_underlying(id)])).load(std::memory_order_acquire));
		}

		static size_t POOL_SIZE = 64 * 1024;

		// Page bodies per pool class; a class that fills its slots falls back to large blocks.
		inline constexpr size_t page_slots_per_pool = 2048;
		// Address space per large class; a full class spills into the next larger one.
		inline constexpr size_t large_class_budget = 16ull * 1024 * 1024 * 1024;
		inline constexpr std::array<size_t, pool_count> page_body_sizes{
			page_geometry<block8_t>::body_bytes, page_geometry<block16_t>::body_bytes,
			page_geometry<block32_t>::body_bytes, page_geometry<block64_t>::body_bytes,
			page_geometry<block128_t>::body_bytes, page_geometry<block256_t>::body_bytes,
			page_geometry<block512_t>::body_bytes, page_geometry<block1024_t>::body_bytes,
			page_geometry<block4096_t>::body_bytes, page_geometry<block8192_t>::body_bytes,
			page_geometry<block16384_t>::body_bytes, page_geometry<block32768_t>::body_bytes,
			page_geometry<block65536_t>::body_bytes, page_geometry<block131072_t>::body_bytes
		};

		[[nodiscard]] constexpr size_t round_up(size_t value, size_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		[[nodiscard]] constexpr size_t large_slot_size(size_t index) noexcept
		{
			return large_slot_minimum << index;
		}

		[[nodiscard]] constexpr size_t large_slot_count(size_t index) noexcept
		{
			return large_class_budget / large_slot_size(index) < 4 ? 4 : large_class_budget / large_slot_size(index);
		}

		// Two threads can create a lazy pool at once: publish exactly one, or a block gets released through the wrong pool.
		template<typename _type>
		static _type* acquire_pool(void** pools, pool_type id, core::mapper* mapper) noexcept
		{
			std::atomic_ref<void*> slot(pools[std::to_underlying(id)]);
			if (auto* existing = slot.load(std::memory_order_acquire))
				return reinterpret_cast<_type*>(existing);

			auto* created = new _type(POOL_SIZE, mapper);
			void* expected = nullptr;
			if (slot.compare_exchange_strong(expected, reinterpret_cast<void*>(created),
				std::memory_order_acq_rel, std::memory_order_acquire))
				return created;

			delete created;
			return reinterpret_cast<_type*>(expected);
		}

		memory_manager::memory_manager()
		{
			core::initialize();

			size_t reserve = core::region::granularity;
			for (const auto body : page_body_sizes)
				reserve += round_up(body, core::region::granularity) * page_slots_per_pool;
			for (size_t index = 0; index < large_class_count; ++index)
				reserve += large_slot_size(index) * large_slot_count(index);
			auto* cursor = core::region::reserve(reserve);
			if (!cursor || !core::region::commit(cursor, sizeof(block_base)))
			{
				_vassert(!cursor);
				return;
			}
			// The zero-size block sits in the region too, so every pointer handed out passes the range check.
			zero_size_request_block = create_default_block(reinterpret_cast<block_base*>(cursor), 0);
			cursor += core::region::granularity;
			for (size_t index = 0; index < page_sources.size(); ++index)
			{
				const auto slot = round_up(page_body_sizes[index], core::region::granularity);
				page_sources[index].assign(cursor, slot, page_slots_per_pool);
				cursor += slot * page_slots_per_pool;
			}
			for (size_t index = 0; index < large_sources.size(); ++index)
			{
				large_sources[index].assign(cursor, large_slot_size(index), large_slot_count(index));
				cursor += large_slot_size(index) * large_slot_count(index);
			}

			pools = voltek::core::_internal::aligned_talloc<void*>(pool_count, 0x10);
			if (pools)
			{
				for (const auto id : { pool_type::pool_8, pool_type::pool_16, pool_type::pool_32, pool_type::pool_64 })
				{
					visit_pool(id, [&]<typename Pool>() {
						(void)acquire_pool<Pool>(pools, id, &page_sources[std::to_underlying(id)]);
					});
				}
			}
		}

		// The manager lives for the whole process; its region and pools are never torn down.
		memory_manager::~memory_manager() = default;

		bool memory_manager::owns(const void* ptr) noexcept
		{
			const auto* header = get_block_handle_from_ptr(ptr);
			return core::region::contains(header) && is_valid_block(header);
		}

		block_base* memory_manager::large_alloc(size_t size) noexcept
		{
			for (size_t index = 0; index < large_sources.size(); ++index)
			{
				if (large_slot_size(index) < size)
					continue;
				if (auto* block = large_sources[index].allocate(size))
				{
					large_counters.live_blocks.fetch_add(1, std::memory_order_relaxed);
					large_counters.allocations.fetch_add(1, std::memory_order_relaxed);
					large_counters.allocated_bytes.fetch_add(size, std::memory_order_relaxed);
					large_counters.requested_bytes.fetch_add(size, std::memory_order_relaxed);
					return static_cast<block_base*>(block);
				}
			}
			return nullptr;
		}

		void memory_manager::large_free(block_base* block, size_t size) noexcept
		{
			for (auto& source : large_sources)
			{
				if (source.contains(block))
				{
					source.release(block);
					large_counters.live_blocks.fetch_sub(1, std::memory_order_relaxed);
					large_counters.requested_bytes.fetch_sub(size, std::memory_order_relaxed);
					return;
				}
			}
		}

		void* memory_manager::alloc(size_t size) noexcept
		{
			if (!size)
				return zero_size_request_block ? get_ptr_from_block_handle(zero_size_request_block) : nullptr;

			if (pools && size <= pool_limit_maximum)
			{
				const auto id = pool_class_of(size);
				auto* pooled = visit_pool(id, [&]<typename Pool>() -> void* {
					auto* pool = acquire_pool<Pool>(pools, id, &page_sources[std::to_underlying(id)]);
					typename Pool::pageptr_t page = nullptr;
					typename Pool::block_type* block = nullptr;
					size_t index_block = 0;
					const auto counted = statistics_enabled.load(std::memory_order_relaxed);
					if (!pool || !pool->get_free_block(block, page, index_block, size, counted))
						return nullptr;
					create_pool_block(block, static_cast<uint32_t>(size), static_cast<uint16_t>(page->get_user_data()),
						static_cast<uint32_t>(index_block), static_cast<uint8_t>(id));
					if (counted)
						block->flags |= flag_block_counted;
					return get_ptr_from_block_handle(block);
				});
				// A pool that cannot grow falls through to a large block.
				if (pooled)
					return pooled;
			}

			if (size > SIZE_MAX - sizeof(block_base))
				return nullptr;
			if (auto* block = large_alloc(size + sizeof(block_base)))
			{
				create_default_block(block, size);
				return get_ptr_from_block_handle(block);
			}
			return nullptr;
		}

		void* memory_manager::aligned_alloc(size_t size, size_t alignment) noexcept
		{
			if (!alignment)
				alignment = 16;
			if ((alignment & (alignment - 1)) != 0)
				return nullptr;
			if (alignment <= 16)
				return alloc(size);

			if (alignment - 1 > SIZE_MAX - sizeof(aligned_block))
				return nullptr;
			const auto overhead = alignment - 1 + sizeof(aligned_block);
			if (size > SIZE_MAX - overhead)
				return nullptr;

			void* base = alloc(size + overhead);
			if (!base)
				return nullptr;

			const auto address = (reinterpret_cast<uintptr_t>(base) +
				sizeof(aligned_block) + alignment - 1) & ~(alignment - 1);
			auto* block = reinterpret_cast<aligned_block*>(address - sizeof(aligned_block));
			block->base = base;
			block->alignment = alignment;
			create_default_block(&block->header, size);
			block->header.flags |= flag_block_aligned;
			return reinterpret_cast<void*>(address);
		}

		void* memory_manager::aligned_realloc(const void* ptr, size_t size, size_t alignment) noexcept
		{
			if (!alignment)
				alignment = 16;
			if ((alignment & (alignment - 1)) != 0)
				return nullptr;
			if (!ptr)
				return aligned_alloc(size, alignment);
			if (!owns(ptr))
				return nullptr;
			if (!size)
			{
				free(ptr);
				return nullptr;
			}

			auto* block = get_block_handle_from_ptr(ptr);
			if (!is_used_aligned_block(block) && alignment <= 16)
				return realloc(ptr, size);

			void* replacement = aligned_alloc(size, alignment);
			if (!replacement)
				return nullptr;
			const auto old_size = get_size_from_block(block);
			memcpy(replacement, ptr, old_size < size ? old_size : size);
			free(ptr);
			return replacement;
		}

		void* memory_manager::realloc(const void* ptr, size_t size) noexcept
		{
			if (!ptr || !owns(ptr))
				return nullptr;
			if (!size)
			{
				free(ptr);
				return nullptr;
			}

			auto* block = get_block_handle_from_ptr(ptr);
			if (is_used_aligned_block(block))
				return aligned_realloc(ptr, size, get_aligned_block(block)->alignment);

			// A pooled block keeps its slot while the new size still fits its class.
			if (is_used_pool_block(block) && block->pool_id < pool_count && size <= pool_limits[block->pool_id] && pools)
			{
				const auto id = static_cast<pool_type>(block->pool_id);
				if (block->flags & flag_block_counted)
				{
					visit_pool(id, [&]<typename Pool>() {
						if (auto* pool = loaded_pool<Pool>(pools, id))
							pool->resize_requested(block->size, size);
					});
				}
				block->size = static_cast<uint32_t>(size);
				return const_cast<void*>(ptr);
			}

			const auto old_size = get_size_from_block(block);
			void* replacement = alloc(size);
			if (!replacement)
				return nullptr;
			if (old_size)
				memcpy(replacement, ptr, old_size < size ? old_size : size);
			free(ptr);
			return replacement;
		}

		bool memory_manager::free(const void* ptr) noexcept
		{
			if (!ptr || !owns(ptr))
				return false;

			auto* block = get_block_handle_from_ptr(ptr);
			if (is_used_aligned_block(block))
				return free(get_aligned_block(block)->base);

			if (is_used_default_block(block))
			{
				// The zero-size block is never released.
				if (const auto size = get_size_from_block(block))
					large_free(block, size + sizeof(block_base));
				return true;
			}

			if (!pools || block->pool_id >= pool_count)
				return false;
			const auto id = static_cast<pool_type>(block->pool_id);
			return visit_pool(id, [&]<typename Pool>() {
				auto* pool = loaded_pool<Pool>(pools, id);
				return pool && pool->release_block((*pool)[block->page_id], block->block_id, block->size,
					(block->flags & flag_block_counted) != 0);
			});
		}

		size_t memory_manager::msize(const void* ptr) const noexcept
		{
			if (!ptr || !owns(ptr)) return 0;
			// Получение размера.
			return (size_t)get_size_from_ptr(ptr);
		}

		void memory_manager::dump_map(size_t pool_id, const char* filename) const noexcept
		{
#ifndef VMMDLL_EXPORTS
			if (pool_id >= pool_count || !filename || !pools)
				return;
			const auto id = static_cast<pool_type>(pool_id);
			visit_pool(id, [&]<typename Pool>() {
				if (auto* pool = loaded_pool<Pool>(pools, id))
					pool->dump_map(filename);
			});
#endif // !VMMDLL_EXPORTS
		}

		void memory_manager::dump(size_t pool_id, const char* filename) const noexcept
		{
#ifndef VMMDLL_EXPORTS
			if (pool_id >= pool_count || !filename || !pools)
				return;
			const auto id = static_cast<pool_type>(pool_id);
			visit_pool(id, [&]<typename Pool>() {
				if (auto* pool = loaded_pool<Pool>(pools, id))
					pool->dump(filename);
			});
#endif // !VMMDLL_EXPORTS
		}

		void memory_manager::pool_stats(scalable_pool_stats& out) const noexcept
		{
			out = {};
			if (!pools)
				return;
			for (size_t index = 0; index < pool_count; ++index)
			{
				const auto id = static_cast<pool_type>(index);
				visit_pool(id, [&]<typename Pool>() {
					auto* pool = loaded_pool<Pool>(pools, id);
					if (!pool)
						return;
					// Unlocked maintained counters may produce a marginally stale sample.
					out.pool_count += !pool->empty();
					out.page_capacity += pool->count();
					out.pages_busy += pool->busy_count();
				});
			}
		}

		size_t memory_manager::class_stats(scalable_class_stats* out, size_t capacity) const noexcept
		{
			const auto read = [](const std::atomic<uint64_t>& a_counter) { return a_counter.load(std::memory_order_relaxed); };
			size_t count = 0;
			for (size_t index = 0; index < pool_count && count < capacity; ++index, ++count)
			{
				auto& entry = out[count];
				entry = {};
				entry.request_limit = pool_limits[index];
				entry.committed_bytes = page_sources[index].committed_bytes();
				if (!pools)
					continue;
				const auto id = static_cast<pool_type>(index);
				visit_pool(id, [&]<typename Pool>() {
					auto* pool = loaded_pool<Pool>(pools, id);
					if (!pool)
						return;
					const auto& counters = pool->counters();
					entry.block_stride = sizeof(typename Pool::block_type);
					entry.live_blocks = read(counters.live_blocks);
					entry.requested_bytes = counters.live_requested_bytes();
					entry.allocations = read(counters.allocations);
					entry.allocated_bytes = read(counters.allocated_bytes);
					entry.pages_created = read(counters.pages_created);
					entry.pages_released = read(counters.pages_released);
					entry.scan_words = read(counters.scan_words);
					entry.lock_contended = read(counters.lock.contended);
					entry.lock_wait_ticks = read(counters.lock.wait_ticks);
				});
			}
			if (count < capacity)
			{
				auto& entry = out[count++];
				entry = {};
				for (const auto& source : large_sources)
					entry.committed_bytes += source.committed_bytes();
				entry.live_blocks = read(large_counters.live_blocks);
				entry.requested_bytes = read(large_counters.requested_bytes);
				entry.allocations = read(large_counters.allocations);
				entry.allocated_bytes = read(large_counters.allocated_bytes);
			}
			return count;
		}
	}

	VOLTEK_MM_API void scalable_get_pool_stats(scalable_pool_stats* out)
	{
		if (!out)
			return;
		*out = {};
		if (auto* manager = memory_manager::global_memory_manager)
			manager->pool_stats(*out);
	}

	VOLTEK_MM_API void scalable_enable_statistics()
	{
		memory_manager::statistics_enabled.store(true, std::memory_order_relaxed);
	}

	VOLTEK_MM_API size_t scalable_get_class_stats(scalable_class_stats* out, size_t capacity)
	{
		auto* manager = memory_manager::global_memory_manager;
		return manager && out ? manager->class_stats(out, capacity) : 0;
	}

	VOLTEK_MM_API void scalable_get_memory_stats(scalable_memory_stats* out)
	{
		if (!out)
			return;
		*out = {};
		out->reserved_bytes = core::region::reserved();
		auto* manager = memory_manager::global_memory_manager;
		if (!manager)
			return;
		scalable_class_stats classes[memory_manager::pool_count + 1]{};
		const auto count = manager->class_stats(classes, std::size(classes));
		for (size_t index = 0; index < count; ++index)
		{
			out->committed_bytes += classes[index].committed_bytes;
			out->live_blocks += classes[index].live_blocks;
			out->requested_bytes += classes[index].requested_bytes;
		}
	}
}

#pragma warning(default : 26819)
