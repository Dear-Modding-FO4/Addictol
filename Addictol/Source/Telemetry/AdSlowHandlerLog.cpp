#include <Telemetry/AdSlowHandlerLog.h>
#include <Core/AdClock.h>
#include <REX/REX.h>

#include <algorithm>
#include <format>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		constexpr std::string_view kSeriesPrefix{ "plugin." };

		[[nodiscard]] double Seconds(uint64_t a_ticks) noexcept
		{
			return QpcToMilliseconds(a_ticks, GetQpcFrequency()) / 1000.0;
		}
	}

	SlowHandlerLog& SlowHandlerLog::Get() noexcept
	{
		// Plugin callbacks can outlive ordinary static destruction.
		static auto* log = new SlowHandlerLog;
		return *log;
	}

	void SlowHandlerLog::Arm() noexcept
	{
		const auto frequency = GetQpcFrequency();
		if (frequency)
			m_thresholdTicks = frequency * kThresholdMs / 1000;
	}

	void SlowHandlerLog::Report(std::string_view a_plugin, std::string_view a_series, uint64_t a_ticks) noexcept
	{
		const auto what = a_series.starts_with(kSeriesPrefix) ? a_series.substr(kSeriesPrefix.size()) : a_series;
		REX::WARN("Slow plugin: {} {} took {:.2f} s"sv, a_plugin, what, Seconds(a_ticks));

		const std::scoped_lock lock{ m_mutex };
		if (!m_summarized && m_startup.size() < kStartupCapacity)
			m_startup.push_back({ std::string{ a_plugin }, what, a_ticks });
	}

	void SlowHandlerLog::LogStartupSummary() noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		m_summarized = true;
		if (m_startup.empty())
			return;

		std::ranges::sort(m_startup, std::greater{}, &Entry::ticks);
		uint64_t total{};
		std::string list;
		for (const auto& entry : m_startup)
		{
			total += entry.ticks;
			list += std::format("{}{} {} {:.2f} s"sv, list.empty() ? ""sv : ", "sv, entry.plugin, entry.what, Seconds(entry.ticks));
		}
		REX::WARN("Slow plugins delayed startup by {:.2f} s: {}"sv, Seconds(total), list);
		m_startup.clear();
	}
}
