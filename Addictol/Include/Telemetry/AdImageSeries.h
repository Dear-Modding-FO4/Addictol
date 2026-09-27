#pragma once

#include <Telemetry/AdImageRegistry.h>
#include <Telemetry/AdTelemetry.h>
#include <Core/Settings/AdSettings.h>

#include <cstdio>

namespace Addictol
{
	inline constexpr size_t kImageSeriesCapacity{ 1024 };

	[[nodiscard]] inline size_t ImageSeriesCapacity(bool a_enabled) noexcept
	{
		return a_enabled && bTelemetryEnabled.GetValue() ? kImageSeriesCapacity : 0;
	}

	struct ImageSeriesKind
	{
		std::string_view series;
		std::string_view role{};
		bool gauge{};
	};

	template<size_t KindCount, bool QualifiedBuckets = false>
	class ImageSeriesTable
	{
	public:
		struct Counter
		{
			std::atomic<uint64_t> calls{}, ticks{}, bytes{};
		};

		explicit ImageSeriesTable(std::span<const ImageSeriesKind, KindCount> a_kinds,
			size_t a_capacity = kImageSeriesCapacity) noexcept :
			m_kinds(a_kinds), m_capacity((std::min)(a_capacity, kImageSeriesCapacity))
		{}

		[[nodiscard]] Counter& At(ImageId a_image, size_t a_kind) noexcept { return m_counters[a_image][a_kind]; }
		[[nodiscard]] const Counter& At(ImageId a_image, size_t a_kind) const noexcept { return m_counters[a_image][a_kind]; }
		void BeginInterval() noexcept { m_draining = true; }
		[[nodiscard]] bool Draining() const noexcept { return m_draining; }
		[[nodiscard]] uint64_t TakeOverflow() noexcept { return std::exchange(m_overflow, 0); }

		[[nodiscard]] size_t Drain(std::span<SeriesSample> a_out, double a_tickScale = 1.0) noexcept
		{
			if (!m_draining || a_out.empty())
				return 0;
			a_out = a_out.first((std::min)(a_out.size(), m_capacity));
			const auto count = ImageRegistry::Get().Count() * KindCount;
			size_t offset{};
			for (size_t index = 0; index < count; ++index)
			{
				const auto row = (m_cursor + index) % count;
				const auto image = static_cast<ImageId>(row / KindCount);
				const auto kind = row % KindCount;
				auto& counter = At(image, kind);
				const auto& descriptor = m_kinds[kind];
				const auto read = [&](std::atomic<uint64_t>& a_value) {
					return descriptor.gauge ? a_value.load(std::memory_order_relaxed) :
						a_value.exchange(0, std::memory_order_relaxed);
				};
				const Values now{ read(counter.calls), read(counter.ticks), read(counter.bytes) };
				auto& before = m_previous[image][kind];
				if (descriptor.gauge ? now == before : !(now.calls || now.ticks || now.bytes))
					continue;
				if (offset == a_out.size())
				{
					++m_overflow;
					continue;
				}
				std::string_view bucket = ImageRegistry::Get().At(image)->name;
				if constexpr (QualifiedBuckets)
				{
					auto& name = m_names[row];
					if (!name[0])
						std::snprintf(name.data(), name.size(), "%.*s@%.*s",
							static_cast<int>(bucket.size()), bucket.data(),
							static_cast<int>(descriptor.role.size()), descriptor.role.data());
					bucket = name.data();
				}
				const auto ticks = a_tickScale == 1.0 ? now.ticks : static_cast<uint64_t>(now.ticks * a_tickScale);
				(void)AppendSeriesSample(a_out, offset, { descriptor.series, bucket, now.calls, ticks, now.bytes, descriptor.gauge });
				if (descriptor.gauge)
					before = now;
			}
			// Rotate priority so persistent gauges cannot starve behind busy low-numbered images.
			if (count)
				m_cursor = (m_cursor + m_capacity) % count;
			return offset;
		}

	private:
		struct Values
		{
			uint64_t calls{}, ticks{}, bytes{};
			bool operator==(const Values&) const noexcept = default;
		};
		std::span<const ImageSeriesKind, KindCount> m_kinds;
		size_t m_capacity;
		std::array<std::array<Counter, KindCount>, kImageCapacity> m_counters{};
		std::array<std::array<Values, KindCount>, kImageCapacity> m_previous{};
		std::array<std::array<char, 272>, QualifiedBuckets ? kImageCapacity * KindCount : 0> m_names{};
		size_t m_cursor{};
		uint64_t m_overflow{};
		bool m_draining{};
	};

	template<size_t KindCount, bool QualifiedBuckets = false>
	class ImageSeriesSource : public MetricSource, public SeriesSource
	{
	public:
		[[nodiscard]] size_t SeriesCapacity() const noexcept override
		{
			return ImageSeriesCapacity(m_option.GetValue());
		}

	protected:
		ImageSeriesSource(const BoolSetting& a_option, std::span<const ImageSeriesKind, KindCount> a_kinds) noexcept :
			m_rows(a_kinds), m_option(a_option)
		{}

		[[nodiscard]] virtual double TickScale() const noexcept { return 1.0; }
		ImageSeriesTable<KindCount, QualifiedBuckets> m_rows;

	private:
		void BeginInterval(uint64_t) noexcept final { m_rows.BeginInterval(); }
		[[nodiscard]] size_t DrainSeries(std::span<SeriesSample> a_out) noexcept final
		{
			if (a_out.empty() || !m_rows.Draining())
				return 0;
			return m_rows.Drain(a_out, TickScale());
		}
		const BoolSetting& m_option;
	};
}
