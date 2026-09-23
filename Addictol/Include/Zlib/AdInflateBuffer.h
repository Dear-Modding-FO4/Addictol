#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Addictol
{
	struct InflateBufferPool;

	class InflateBuffer
	{
	public:
		// Limit decoded objects to 16 MiB, pooled storage to 32 MiB/thread and 128 MiB/process.
		inline static constexpr size_t MAX_CAPACITY = 16 * 1024 * 1024;
		InflateBuffer() noexcept = default;
		InflateBuffer(const InflateBuffer&) = delete;
		InflateBuffer& operator=(const InflateBuffer&) = delete;
		InflateBuffer(InflateBuffer&& a_other) noexcept { Swap(a_other); }
		InflateBuffer& operator=(InflateBuffer&& a_other) noexcept
		{
			if (this != &a_other) { Reset(); Swap(a_other); }
			return *this;
		}
		~InflateBuffer() noexcept { Reset(); }
		bool Acquire(size_t a_capacity) noexcept;
		void Reset() noexcept;
		std::span<uint8_t> Bytes() const noexcept { return { m_data, m_capacity }; }

	private:
		void Swap(InflateBuffer& a_other) noexcept
		{
			std::swap(m_pool, a_other.m_pool);
			std::swap(m_data, a_other.m_data);
			std::swap(m_capacity, a_other.m_capacity);
		}
		InflateBufferPool* m_pool{};
		uint8_t* m_data{};
		size_t m_capacity{};
	};
}
