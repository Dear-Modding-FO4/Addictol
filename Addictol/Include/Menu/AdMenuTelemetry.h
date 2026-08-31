#pragma once

#include <Menu/AdMenuTargets.h>
#include <Telemetry/AdTelemetry.h>

#include <array>
#include <cstdio>

namespace Addictol
{
	void DrawMenuTelemetryPanel(void* a_context) noexcept;

	enum class TelemetryPanel : uint8_t
	{
		kOverview,
		kMemory,
		kDecompression,
		kStability,
		kAudio,
		kCount,
		kNone = 0xFF
	};

	struct TelemetryPanelDefinition
	{
		TelemetryPanel panel;
		const char* id;
		std::string_view name;
		std::string_view description;
		int32_t sortKey;
	};

	inline constexpr std::array kTelemetryPanels{
		TelemetryPanelDefinition{ TelemetryPanel::kOverview, "overview", "Overview"sv,
			"Frame pacing and the signals most likely to need attention."sv, 0 },
		TelemetryPanelDefinition{ TelemetryPanel::kMemory, "memory", "Memory"sv,
			"Process, system, video, and allocator memory."sv, 10 },
		TelemetryPanelDefinition{ TelemetryPanel::kDecompression, "decompression", "Decompression"sv,
			"Libdeflate interval totals and zlib workload distribution."sv, 20 },
		TelemetryPanelDefinition{ TelemetryPanel::kStability, "stability", "Stability"sv,
			"Escape recovery, reference handles, and module outcomes."sv, 30 },
		TelemetryPanelDefinition{ TelemetryPanel::kAudio, "audio", "Audio"sv,
			"XAudio2 voice, latency, memory, and glitch telemetry."sv, 40 }
	};

	struct TelemetryMetricGroup
	{
		std::string_view prefix;
		std::string_view heading;
		TelemetryPanel panel;
	};

	inline constexpr std::array kTelemetryMetricGroups{
		TelemetryMetricGroup{ "frame."sv, "Frame"sv, TelemetryPanel::kOverview },
		TelemetryMetricGroup{ "process."sv, "Process"sv, TelemetryPanel::kMemory },
		TelemetryMetricGroup{ "system."sv, "System"sv, TelemetryPanel::kMemory },
		TelemetryMetricGroup{ "gpu."sv, "Video memory"sv, TelemetryPanel::kMemory },
		TelemetryMetricGroup{ "allocator."sv, "Allocator"sv, TelemetryPanel::kMemory },
		TelemetryMetricGroup{ "libdeflate."sv, "Libdeflate"sv, TelemetryPanel::kDecompression },
		TelemetryMetricGroup{ "escape."sv, "Escape recovery"sv, TelemetryPanel::kStability },
		TelemetryMetricGroup{ "references."sv, "Reference handles"sv, TelemetryPanel::kStability },
		TelemetryMetricGroup{ "modules."sv, "Module outcomes"sv, TelemetryPanel::kStability },
		TelemetryMetricGroup{ "plugin."sv, "Plugin timing"sv, TelemetryPanel::kStability },
		TelemetryMetricGroup{ "esp."sv, "Form loading"sv, TelemetryPanel::kStability },
		TelemetryMetricGroup{ "audio."sv, "XAudio2"sv, TelemetryPanel::kAudio }
	};

	inline constexpr std::array kTelemetryOverviewMetrics{
		"frame.mean_ms"sv, "frame.max_ms"sv, "frame.min_ms"sv, "frame.max_offset_ms"sv,
		"process.working_set_bytes"sv, "gpu.vram_used_bytes"sv,
		"references.handle_usage"sv, "modules.install_failures"sv, "audio.glitches"sv
	};

	struct TelemetryMetricClassification
	{
		TelemetryPanel panel{ TelemetryPanel::kNone };
		size_t matches{ 0 };
	};

	[[nodiscard]] constexpr TelemetryMetricClassification ClassifyTelemetryMetric(
		std::string_view a_key) noexcept
	{
		TelemetryMetricClassification result{};
		for (const auto& group : kTelemetryMetricGroups)
		{
			if (!a_key.starts_with(group.prefix))
				continue;
			result.panel = group.panel;
			++result.matches;
		}
		if (result.matches != 1)
			result.panel = TelemetryPanel::kNone;
		return result;
	}

	[[nodiscard]] constexpr std::string_view TelemetryMetricLabel(
		std::string_view a_key) noexcept
	{
		const auto separator = a_key.find('.');
		return separator == std::string_view::npos ? a_key : a_key.substr(separator + 1);
	}

	[[nodiscard]] constexpr bool IsCumulativeTelemetryMetric(
		std::string_view a_key) noexcept
	{
		return a_key.starts_with("escape."sv) ||
			a_key.starts_with("modules."sv) ||
			a_key == "audio.glitches"sv;
	}

	struct TelemetryValueDisplay
	{
		std::array<char, 48> text{};
		size_t length{ 0 };
		float fraction{ 0.0f };
		bool valid{ false };
		bool progress{ false };

		[[nodiscard]] std::string_view Text() const noexcept { return { text.data(), length }; }
	};

	[[nodiscard]] constexpr float TelemetryPercentFraction(double a_percent) noexcept
	{
		const auto fraction = a_percent / 100.0;
		return static_cast<float>(
			fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction);
	}

	[[nodiscard]] inline TelemetryValueDisplay FormatTelemetryValue(
		MetricValue a_value,
		Unit a_unit) noexcept
	{
		if (!a_value.valid)
		{
			TelemetryValueDisplay display{};
			display.text[0] = '-';
			display.length = 1;
			return display;
		}

		TelemetryValueDisplay display{};
		display.valid = true;
		int written{ 0 };
		if (a_unit == Unit::kBytes)
		{
			static constexpr std::array units{ "B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv };
			size_t unit{ 0 };
			while (a_value.value >= 1024.0 && unit + 1 < units.size())
			{
				a_value.value /= 1024.0;
				++unit;
			}
			written = unit ?
				std::snprintf(display.text.data(), display.text.size(), "%.2f %.*s",
					a_value.value, static_cast<int>(units[unit].size()), units[unit].data()) :
				std::snprintf(display.text.data(), display.text.size(), "%.0f B", a_value.value);
		}
		else if (a_unit == Unit::kPercent)
		{
			display.progress = true;
			display.fraction = TelemetryPercentFraction(a_value.value);
			written = std::snprintf(
				display.text.data(), display.text.size(), "%.2f%%", a_value.value);
		}
		else
		{
			written = std::snprintf(
				display.text.data(),
				display.text.size(),
				a_unit == Unit::kMilliseconds ? "%.3f ms" : "%.0f",
				a_value.value);
		}
		display.length = ClampMenuFormattedLength(written, display.text.size());
		return display;
	}
}
