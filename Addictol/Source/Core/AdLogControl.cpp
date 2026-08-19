#include <Core/AdLogControl.h>

#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>

namespace Addictol::LogControl
{
	using namespace std::literals;

#ifdef COMMONLIB_OPTION_TOML
	static REX::TOML::Str<> sAdditionalLogLevel{ "Additional"sv, "sLogLevel"sv, "info" };
	static REX::TOML::Str<> sAdditionalLogFlushLevel{ "Additional"sv, "sLogFlushLevel"sv, "info" };
#endif

	namespace
	{
		constexpr uint64_t kBucketCount = 60;
		constexpr uint64_t kCountMask = std::numeric_limits<uint32_t>::max();

		// Each bucket packs its second into the high half and the count into the low half of one
		// atomic, so a reader can never observe a count against the wrong second.

		class CountingSink final : public spdlog::sinks::sink
		{
		public:
			void log(const spdlog::details::log_msg&) override
			{
				_written.fetch_add(1, std::memory_order_relaxed);

				const auto second = CurrentSecond();
				auto& bucket = _buckets[second % kBucketCount];
				auto packed = bucket.load(std::memory_order_relaxed);
				for (;;)
				{
					const auto epoch = packed >> 32;
					const auto count = packed & kCountMask;
					const auto replacement = epoch == second ?
						(second << 32) | std::min(count + 1, kCountMask) :
						(second << 32) | 1;
					if (bucket.compare_exchange_weak(
							packed, replacement, std::memory_order_relaxed))
						break;
				}
			}

			void flush() override
			{
				_flushed.fetch_add(1, std::memory_order_relaxed);
			}

			void set_pattern(const std::string&) override
			{}

			void set_formatter(std::unique_ptr<spdlog::formatter>) override
			{}

			[[nodiscard]] Stats CopyStats() const noexcept
			{
				Stats stats;
				stats.written = _written.load(std::memory_order_relaxed);
				stats.flushed = _flushed.load(std::memory_order_relaxed);

				const auto now = CurrentSecond();
				uint64_t recentLines = 0;
				for (const auto& bucket : _buckets)
				{
					const auto packed = bucket.load(std::memory_order_relaxed);
					const auto epoch = packed >> 32;
					if (epoch <= now && now - epoch < kBucketCount)
						recentLines += packed & kCountMask;
				}
				stats.linesPerMinute = static_cast<double>(recentLines);
				return stats;
			}

		private:
			[[nodiscard]] static uint64_t CurrentSecond() noexcept
			{
				return static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::seconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count());
			}

			std::atomic<uint64_t> _written{};
			std::atomic<uint64_t> _flushed{};
			std::array<std::atomic<uint64_t>, kBucketCount> _buckets{};
		};

		std::shared_ptr<CountingSink> g_countingSink;
		std::once_flag g_installOnce;

		struct LevelEntry
		{
			Level level;
			std::string_view name;
			spdlog::level::level_enum spdlogLevel;
		};

		constexpr std::array LEVEL_NAMES{
			LevelEntry{ Level::kTrace, "trace", spdlog::level::trace },
			LevelEntry{ Level::kDebug, "debug", spdlog::level::debug },
			LevelEntry{ Level::kInfo, "info", spdlog::level::info },
			LevelEntry{ Level::kWarn, "warn", spdlog::level::warn },
			LevelEntry{ Level::kError, "error", spdlog::level::err },
			LevelEntry{ Level::kCritical, "critical", spdlog::level::critical },
			LevelEntry{ Level::kOff, "off", spdlog::level::off }
		};

		[[nodiscard]] constexpr spdlog::level::level_enum ToSpdlog(Level a_level) noexcept
		{
			for (const auto& entry : LEVEL_NAMES)
			{
				if (entry.level == a_level)
					return entry.spdlogLevel;
			}
			return spdlog::level::off;
		}

		[[nodiscard]] constexpr Level FromSpdlog(spdlog::level::level_enum a_level) noexcept
		{
			for (const auto& entry : LEVEL_NAMES)
			{
				if (entry.spdlogLevel == a_level)
					return entry.level;
			}
			return Level::kOff;
		}

		[[nodiscard]] constexpr bool EqualIgnoreCase(
			std::string_view a_left,
			std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size())
				return false;

			for (size_t index = 0; index < a_left.size(); ++index)
			{
				auto left = static_cast<unsigned char>(a_left[index]);
				auto right = static_cast<unsigned char>(a_right[index]);
				if (left >= 'A' && left <= 'Z')
					left = static_cast<unsigned char>(left + ('a' - 'A'));
				if (right >= 'A' && right <= 'Z')
					right = static_cast<unsigned char>(right + ('a' - 'A'));
				if (left != right)
					return false;
			}
			return true;
		}

		[[nodiscard]] Level ParseConfiguredLevel(
			std::string_view a_value,
			std::string_view a_key) noexcept
		{
			if (const auto parsed = ParseLevel(a_value))
				return *parsed;

			spdlog::warn(
				"Config: {}=\"{}\" is invalid; using info.",
				a_key,
				a_value);
			return Level::kInfo;
		}
	}

	std::string_view LevelName(Level a_level) noexcept
	{
		for (const auto& level : LEVEL_NAMES)
		{
			if (level.level == a_level)
				return level.name;
		}
		return "off";
	}

	std::optional<Level> ParseLevel(std::string_view a_name) noexcept
	{
		for (const auto& level : LEVEL_NAMES)
		{
			if (EqualIgnoreCase(a_name, level.name))
				return level.level;
		}
		return std::nullopt;
	}

	void Install() noexcept
	{
		try
		{
			std::call_once(g_installOnce, [] {
				auto logger = spdlog::default_logger();
				auto sink = std::make_shared<CountingSink>();
				logger->sinks().push_back(sink);
				g_countingSink = std::move(sink);

#ifdef COMMONLIB_OPTION_TOML
				const auto level = ParseConfiguredLevel(
					sAdditionalLogLevel.GetValue(), "sLogLevel");
				const auto flushLevel = ParseConfiguredLevel(
					sAdditionalLogFlushLevel.GetValue(), "sLogFlushLevel");
#else
				const auto level = Level::kInfo;
				const auto flushLevel = Level::kInfo;
#endif
				SetLevel(level);
				SetFlushLevel(flushLevel);
			});
		}
		catch (const std::exception& error)
		{
			spdlog::error("Log control installation failed: {}.", error.what());
		}
		catch (...)
		{
			spdlog::error("Log control installation failed.");
		}
	}

	Level GetLevel() noexcept
	{
		return FromSpdlog(spdlog::default_logger()->level());
	}

	void SetLevel(Level a_level) noexcept
	{
		spdlog::default_logger()->set_level(ToSpdlog(a_level));
	}

	Level GetFlushLevel() noexcept
	{
		return FromSpdlog(spdlog::default_logger()->flush_level());
	}

	void SetFlushLevel(Level a_level) noexcept
	{
		spdlog::default_logger()->flush_on(ToSpdlog(a_level));
	}

	Stats CopyStats() noexcept
	{
		const auto sink = g_countingSink;
		return sink ? sink->CopyStats() : Stats{};
	}
}
