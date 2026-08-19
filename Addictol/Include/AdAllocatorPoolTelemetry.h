#pragma once

#include <AdTelemetry.h>
#include <Voltek.MemoryManager.h>

namespace Addictol::AllocatorPoolTelemetry
{
	[[nodiscard]] inline std::span<const MetricDescriptor> Schema() noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "allocator.pool_count", Unit::kCount },
			MetricDescriptor{ "allocator.pages_busy", Unit::kCount },
			MetricDescriptor{ "allocator.page_capacity", Unit::kCount }
		};
		return schema;
	}

	inline void Populate(
		std::span<MetricValue> a_out,
		bool a_active,
		const voltek::scalable_pool_stats& a_stats) noexcept
	{
		if (a_out.size() != 3)
			return;
		a_out[0] = { static_cast<double>(a_stats.pool_count), a_active };
		a_out[1] = { static_cast<double>(a_stats.pages_busy), a_active };
		a_out[2] = { static_cast<double>(a_stats.page_capacity), a_active };
	}
}
