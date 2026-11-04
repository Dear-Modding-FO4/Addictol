#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol
{
	// Warns about F4SE plugin exports and handlers that stall the startup path.
	class SlowHandlerLog
	{
	public:
		static constexpr uint64_t kThresholdMs{ 1000 };
		static constexpr size_t kStartupCapacity{ 32 };

		[[nodiscard]] static SlowHandlerLog& Get() noexcept;
		// Unarmed, IsSlow is never true.
		void Arm() noexcept;
		[[nodiscard]] bool IsSlow(uint64_t a_ticks) const noexcept { return a_ticks >= m_thresholdTicks; }
		// a_series is a "plugin.*" telemetry series name.
		void Report(std::string_view a_plugin, std::string_view a_series, uint64_t a_ticks) noexcept;
		void LogStartupSummary() noexcept;

	private:
		struct Entry
		{
			std::string plugin;
			std::string_view what;
			uint64_t ticks{};
		};

		uint64_t m_thresholdTicks{ UINT64_MAX };
		std::mutex m_mutex;
		std::vector<Entry> m_startup;
		bool m_summarized{};
	};
}
