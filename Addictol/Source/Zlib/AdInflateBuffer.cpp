#include <Zlib/AdInflateBuffer.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>

namespace Addictol
{
	namespace inflateBufferDetail
	{
		constexpr size_t THREAD_LIMIT = 32 * 1024 * 1024;
		constexpr size_t PROCESS_LIMIT = 128 * 1024 * 1024;
		std::atomic<size_t> s_allocated{};
		struct Block { uint8_t* data{}; size_t capacity{}; };

		void Free(Block& a_block) noexcept
		{
			std::free(a_block.data);
			s_allocated.fetch_sub(a_block.capacity, std::memory_order_relaxed);
			a_block = {};
		}
	}

	struct InflateBufferPool
	{
		std::atomic<uint32_t> references{ 1 };
		std::mutex mutex;
		std::array<inflateBufferDetail::Block, 8> cache{};
		size_t allocated{};
		~InflateBufferPool()
		{
			for (auto& block : cache)
				inflateBufferDetail::Free(block);
		}
		void Release() noexcept
		{
			if (references.fetch_sub(1, std::memory_order_acq_rel) == 1)
				delete this;
		}
	};

	namespace inflateBufferDetail
	{
		struct ThreadPool
		{
			InflateBufferPool* pool{ new (std::nothrow) InflateBufferPool };
			~ThreadPool() { if (pool) pool->Release(); }
		};
	}

	bool InflateBuffer::Acquire(size_t a_capacity) noexcept
	{
		if (a_capacity > MAX_CAPACITY || !a_capacity)
			return false;
		if (m_capacity >= a_capacity)
			return true;
		Reset();
		thread_local inflateBufferDetail::ThreadPool local;
		auto* pool = local.pool;
		if (!pool)
			return false;
		std::lock_guard lock(pool->mutex);
		inflateBufferDetail::Block* best{};
		for (auto& block : pool->cache)
		{
			if (block.capacity >= a_capacity && block.capacity <= std::max<size_t>(64 * 1024, a_capacity * 2) &&
				(!best || block.capacity < best->capacity))
				best = &block;
		}
		if (best)
		{
			m_data = best->data;
			m_capacity = best->capacity;
			*best = {};
		}
		if (!m_data)
		{
			for (auto& block : pool->cache)
			{
				pool->allocated -= block.capacity;
				inflateBufferDetail::Free(block);
			}
			if (pool->allocated + a_capacity > inflateBufferDetail::THREAD_LIMIT)
				return false;
			const auto old = inflateBufferDetail::s_allocated.fetch_add(a_capacity, std::memory_order_relaxed);
			if (old + a_capacity > inflateBufferDetail::PROCESS_LIMIT)
			{
				inflateBufferDetail::s_allocated.fetch_sub(a_capacity, std::memory_order_relaxed);
				return false;
			}
			m_data = static_cast<uint8_t*>(std::malloc(a_capacity));
			if (!m_data)
			{
				inflateBufferDetail::s_allocated.fetch_sub(a_capacity, std::memory_order_relaxed);
				return false;
			}
			m_capacity = a_capacity;
			pool->allocated += a_capacity;
		}
		m_pool = pool;
		pool->references.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void InflateBuffer::Reset() noexcept
	{
		if (!m_pool)
			return;
		{
			std::lock_guard lock(m_pool->mutex);
			bool cached = false;
			for (auto& block : m_pool->cache)
			{
				if (!block.data)
				{
					block = { m_data, m_capacity };
					cached = true;
					break;
				}
			}
			if (!cached)
			{
				inflateBufferDetail::Block block{ m_data, m_capacity };
				m_pool->allocated -= m_capacity;
				inflateBufferDetail::Free(block);
			}
		}
		m_pool->Release();
		m_pool = nullptr;
		m_data = nullptr;
		m_capacity = 0;
	}
}
