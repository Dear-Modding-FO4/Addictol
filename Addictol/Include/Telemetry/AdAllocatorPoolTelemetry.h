#pragma once

#include <Memory/Heaps/AdHeapBackend.h>
#include <Telemetry/AdTelemetry.h>

namespace Addictol::AllocatorPoolTelemetry
{
	inline constexpr size_t kSeriesPerClass{ 6 };

	inline size_t PopulateSeries(std::span<SeriesSample> a_out,
		std::span<const HeapClassStatistics> a_current, std::span<HeapClassStatistics> a_previous) noexcept
	{
		size_t offset = 0;
		for (size_t index = 0; index < a_current.size() && index < a_previous.size(); ++index)
		{
			const auto& now = a_current[index];
			const auto& before = a_previous[index];
			const auto append = [&](std::string_view a_series, uint64_t a_calls, uint64_t a_ticks, uint64_t a_bytes) {
				if (a_calls || a_ticks || a_bytes)
					(void)AppendSeriesSample(a_out, offset, { a_series, now.label, a_calls, a_ticks, a_bytes });
			};
			append("allocator.class.allocations", now.allocations - before.allocations, 0, now.allocatedBytes - before.allocatedBytes);
			append("allocator.class.page_creates", now.pagesCreated - before.pagesCreated, 0, 0);
			append("allocator.class.page_releases", now.pagesReleased - before.pagesReleased, 0, 0);
			append("allocator.class.lock_waits", now.lockContended - before.lockContended, now.lockWaitTicks - before.lockWaitTicks, 0);
			if (now.heldBlocks != before.heldBlocks || now.committedBytes != before.committedBytes)
				(void)AppendSeriesSample(a_out, offset, { "allocator.class.committed", now.label, now.heldBlocks, 0, now.committedBytes, true });
			if (now.liveBlocks != before.liveBlocks || now.requestedBytes != before.requestedBytes)
				(void)AppendSeriesSample(a_out, offset, { "allocator.class.live", now.label, now.liveBlocks, 0, now.requestedBytes, true });
			a_previous[index] = now;
		}
		return offset;
	}

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
