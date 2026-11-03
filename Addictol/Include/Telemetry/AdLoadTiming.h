#pragma once

#include <Telemetry/AdTelemetry.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

namespace Addictol::LoadTiming
{
	inline constexpr std::array<std::string_view, 2> kPluginSeriesNames{
		"plugin.query",
		"plugin.load"
	};
	inline constexpr std::array<std::string_view, 2> kFormSeriesNames{
		"esp.compile",
		"esp.construct"
	};
	// outer pass has no file argument
	inline constexpr std::string_view kCompileBucket{ "all" };

	// Main::StartUp opens the menu right after it starts the game-data thread.
	inline constexpr std::string_view kStartupSeriesName{ "startup" };
	inline constexpr std::string_view kLoadToMenuBucket{ "load_to_menu" };
	inline constexpr std::string_view kLoadToDataReadyBucket{ "load_to_data_ready" };
	inline constexpr std::string_view kMenuToDataReadyBucket{ "menu_to_data_ready" };
	// Continue and New Game proceed once the game-data thread has finished.
	inline constexpr std::string_view kLoadToCanLoadBucket{ "load_to_can_load" };
	inline constexpr std::string_view kMenuToCanLoadBucket{ "menu_to_can_load" };

	inline constexpr std::string_view kInitStepSeriesName{ "startup.init" };

	enum class InitStepFunction : uint8_t
	{
		kInitGameData,
		kThreadProc
	};

	// Time outside these calls is uninstrumented manager and system setup.
	struct InitStepSite
	{
		std::string_view name;
		InitStepFunction function;
		uint32_t ogOffset;
		uint32_t ngaeOffset;
		uint8_t size;
	};

	inline constexpr std::array kInitStepSites{
		InitStepSite{ "init_game_data", InitStepFunction::kThreadProc, 0x024, 0x024, 5 },
		InitStepSite{ "register_archives", InitStepFunction::kInitGameData, 0x02D, 0x031, 5 },
		InitStepSite{ "texture_streamer", InitStepFunction::kInitGameData, 0x032, 0x036, 5 },
		InitStepSite{ "vm_init_wait", InitStepFunction::kInitGameData, 0x224, 0x33A, 6 },
		InitStepSite{ "vm_post_thread_init", InitStepFunction::kInitGameData, 0x24B, 0x361, 5 },
		InitStepSite{ "init_tes", InitStepFunction::kInitGameData, 0x25E, 0x374, 5 },
		InitStepSite{ "post_init_tes", InitStepFunction::kInitGameData, 0x263, 0x379, 5 },
		// F4SE dispatches kGameDataReady to every plugin inside this call.
		InitStepSite{ "data_ready_dispatch", InitStepFunction::kInitGameData, 0x278, 0x38E, 5 }
	};

	inline constexpr std::array kPluginMetricSchema{
		MetricDescriptor{ "plugin.overflow_events", Unit::kCount },
		MetricDescriptor{ "plugin.name_failures", Unit::kCount },
		MetricDescriptor{ "plugin.callback_overflow", Unit::kCount },
		MetricDescriptor{ "plugin.callback_series_overflow", Unit::kCount }
	};
	inline constexpr std::array kFormMetricSchema{
		MetricDescriptor{ "esp.overflow_events", Unit::kCount }
	};

	inline constexpr size_t kPluginBurstCapacity{ 1024 };
	inline constexpr size_t kFormBurstCapacity{ 4608 };
	inline constexpr size_t kPluginNameCapacity{ 512 };
	inline constexpr size_t kModuleFileNameCapacity{ 260 };
	inline constexpr size_t kModulePathCapacity{ 1024 };
	inline constexpr size_t kTESFileNameOffset{ 0x70 };
	inline constexpr size_t kTESFileNameCapacity{ 260 };
	inline constexpr size_t kPluginNameFailureMetric{ 1 };
	inline constexpr size_t kPluginCallbackOverflowMetric{ 2 };
	inline constexpr size_t kPluginCallbackSeriesOverflowMetric{ 3 };

	[[nodiscard]] constexpr std::string_view FileNameFromPath(
		std::string_view a_path) noexcept
	{
		const auto separator = a_path.find_last_of("\\/");
		return separator == std::string_view::npos ? a_path : a_path.substr(separator + 1);
	}

	[[nodiscard]] constexpr bool IsOrdinalProcName(uintptr_t a_procName) noexcept
	{
		return (a_procName >> 16) == 0;
	}

	enum class Runtime : uint8_t
	{
		kOG,
		kNG,
		kAE
	};

	struct TargetId
	{
		uint64_t og;
		uint64_t ng;
		uint64_t ae;
	};

	inline constexpr TargetId kCompileFilesId{ 57137, 2192321, 2192321 };
	inline constexpr TargetId kConstructObjectListId{ 1043280, 2192326, 2192326 };

	inline constexpr auto kCompileFilesSignatureOG = std::to_array<uint8_t>({
		0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57,
		0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
	});
	inline constexpr auto kCompileFilesSignatureNGandAE = std::to_array<uint8_t>({
		0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
		0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
	});

	inline constexpr auto kConstructObjectListSignatureOG = std::to_array<uint8_t>({
		0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
		0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xDA, 0x45, 0x0F, 0xB6,
		0xE8, 0x4C, 0x8B, 0xF9, 0x45, 0x33, 0xC0, 0x33, 0xD2, 0x48, 0x8B, 0xCB
	});
	inline constexpr auto kConstructObjectListSignatureNGandAE = std::to_array<uint8_t>({
		0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41,
		0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
		0x8B, 0xDA, 0x45, 0x0F, 0xB6, 0xE0, 0x4C, 0x8B, 0xE9, 0x45, 0x33, 0xC0,
		0x48, 0x8B, 0xCB, 0x33, 0xD2
	});

	[[nodiscard]] constexpr std::span<const uint8_t> CompileFilesSignature(
		Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kOG ?
			std::span<const uint8_t>{ kCompileFilesSignatureOG } :
			std::span<const uint8_t>{ kCompileFilesSignatureNGandAE };
	}

	[[nodiscard]] constexpr std::span<const uint8_t> ConstructObjectListSignature(
		Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kOG ?
			std::span<const uint8_t>{ kConstructObjectListSignatureOG } :
			std::span<const uint8_t>{ kConstructObjectListSignatureNGandAE };
	}

	[[nodiscard]] constexpr bool ValidateSignature(
		std::ranges::contiguous_range auto&& a_candidate,
		std::span<const uint8_t> a_signature) noexcept
	{
		return !a_signature.empty() && std::ranges::equal(a_candidate, a_signature);
	}

	[[nodiscard]] inline bool ValidateSignature(
		uintptr_t a_target,
		std::span<const uint8_t> a_signature) noexcept
	{
		return ValidateSignature(
			std::span{
				reinterpret_cast<const uint8_t*>(a_target),
				a_signature.size()
			},
			a_signature);
	}
}
