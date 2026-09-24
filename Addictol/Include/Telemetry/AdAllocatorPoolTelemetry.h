#pragma once

#include <Memory/Heaps/AdHeapBackend.h>
#include <Telemetry/AdTelemetry.h>

namespace Addictol::AllocatorPoolTelemetry
{
	[[nodiscard]] inline std::span<const MetricDescriptor> Schema() noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "allocator.pool_count", Unit::kCount },
			MetricDescriptor{ "allocator.pages_busy", Unit::kCount },
			MetricDescriptor{ "allocator.page_capacity", Unit::kCount },
			MetricDescriptor{ "allocator.committed_bytes", Unit::kBytes },
			MetricDescriptor{ "allocator.reserved_bytes", Unit::kBytes },
			MetricDescriptor{ "allocator.live_blocks", Unit::kCount },
			MetricDescriptor{ "allocator.requested_bytes", Unit::kBytes }
		};
		return schema;
	}

	inline void Populate(std::span<MetricValue> a_out, const HeapStatistics& a_stats) noexcept
	{
		const std::array fields{
			a_stats.poolCount, a_stats.pagesBusy, a_stats.pageCapacity,
			a_stats.committedBytes, a_stats.reservedBytes, a_stats.liveBlocks, a_stats.requestedBytes
		};
		if (a_out.size() != fields.size())
			return;
		for (size_t index = 0; index < fields.size(); ++index)
			a_out[index] = { static_cast<double>(fields[index].value_or(0)), fields[index].has_value() };
	}
}
