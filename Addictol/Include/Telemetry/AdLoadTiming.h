#pragma once

#include <Core/AdClock.h>
#include <Core/AdUtils.h>
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
	inline constexpr std::string_view kLoadToStartUpBucket{ "load_to_startup" };
	inline constexpr std::string_view kLoadToMenuBucket{ "load_to_menu" };
	inline constexpr std::string_view kLoadToDataReadyBucket{ "load_to_data_ready" };
	inline constexpr std::string_view kMenuToDataReadyBucket{ "menu_to_data_ready" };
	// Continue and New Game proceed once the game-data thread has finished.
	inline constexpr std::string_view kLoadToCanLoadBucket{ "load_to_can_load" };
	inline constexpr std::string_view kMenuToCanLoadBucket{ "menu_to_can_load" };

	inline constexpr std::string_view kPreMenuStepSeriesName{ "startup.pre_menu" };
	inline constexpr std::string_view kInitStepSeriesName{ "startup.init" };

	enum class InitStepFunction : uint8_t
	{
		kWinMain,
		kStartUp,
		kStartUpArchive,
		kInitGameData,
		kThreadProc
	};

	// Time outside these calls is uninstrumented manager and system setup.
	struct InitStepSite
	{
		std::string_view name;
		InitStepFunction function;
		REL::Offset offset;
		uint8_t size;
		bool og{ true };
	};

	inline constexpr std::array kInitStepSites{
		// Only marks Main::StartUp entry; the call spans the game-data thread start.
		InitStepSite{ "startup_entry", InitStepFunction::kWinMain, REL::Offset{ 0x012 }, 5 },
		InitStepSite{ "havok", InitStepFunction::kStartUp, REL::Offset{ 0x16F, 0x239, 0x2CA }, 5 },
		// OG calls Main_InitRenderer; NG/AE inline it around Renderer::Init.
		InitStepSite{ "renderer", InitStepFunction::kStartUp, REL::Offset{ 0x716, 0xBDD, 0xC6D }, 5 },
		// OG calls Main_InitStartUpArchive; NG/AE inline it into StartUp.
		InitStepSite{ "archive_lists", InitStepFunction::kStartUpArchive, REL::Offset{ 0x028, 0xCAA, 0xD3A }, 5 },
		InitStepSite{ "loose_files", InitStepFunction::kStartUpArchive, REL::Offset{ 0x051, 0xCD3, 0xD63 }, 5 },
		InitStepSite{ "data_prefix", InitStepFunction::kStartUpArchive, REL::Offset{ 0x070, 0xCEB, 0xD7B }, 5 },
		// OG tail-jumps to RefreshFromSources, so there is no call to time.
		InitStepSite{ "texture_refresh", InitStepFunction::kStartUpArchive, REL::Offset{ 0, 0xCF0, 0xD80 }, 5, false },
		InitStepSite{ "scene_graph", InitStepFunction::kStartUp, REL::Offset{ 0x9C4, 0xF1A, 0xFAA }, 5 },
		InitStepSite{ "ui_systems", InitStepFunction::kStartUp, REL::Offset{ 0x9C9, 0xF1F, 0xFAF }, 5 },
		InitStepSite{ "movie_player", InitStepFunction::kStartUp, REL::Offset{ 0xA65, 0xFBB, 0x1050 }, 5 },
		InitStepSite{ "platform", InitStepFunction::kStartUp, REL::Offset{ 0xB80, 0x12B2, 0x1347 }, 5 },
		InitStepSite{ "init_game_data", InitStepFunction::kThreadProc, REL::Offset{ 0x024 }, 5 },
		InitStepSite{ "register_archives", InitStepFunction::kInitGameData, REL::Offset{ 0x02D, 0x031 }, 5 },
		InitStepSite{ "texture_streamer", InitStepFunction::kInitGameData, REL::Offset{ 0x032, 0x036 }, 5 },
		InitStepSite{ "vm_init_wait", InitStepFunction::kInitGameData, REL::Offset{ 0x224, 0x33A }, 6 },
		InitStepSite{ "vm_post_thread_init", InitStepFunction::kInitGameData, REL::Offset{ 0x24B, 0x361 }, 5 },
		InitStepSite{ "init_tes", InitStepFunction::kInitGameData, REL::Offset{ 0x25E, 0x374 }, 5 },
		InitStepSite{ "post_init_tes", InitStepFunction::kInitGameData, REL::Offset{ 0x263, 0x379 }, 5 },
		// F4SE dispatches kGameDataReady to every plugin inside this call.
		InitStepSite{ "data_ready_dispatch", InitStepFunction::kInitGameData, REL::Offset{ 0x278, 0x38E }, 5 }
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

	inline constexpr std::string_view kSaveSeriesName{ "save" };
	// F4SE sends kPreLoadGame/kPostLoadGame around BGSSaveLoadGame::LoadGame.
	inline constexpr std::string_view kPreLoadToPostLoadBucket{ "pre_load_to_post_load" };
	inline constexpr std::string_view kPreLoadToLoadingClosedBucket{ "pre_load_to_loading_closed" };
	inline constexpr std::string_view kSaveStepSeriesName{ "save.load" };
	// GameVM::LoadGame restores Papyrus inside the global-data pass.
	inline constexpr std::array<std::string_view, 4> kSaveStepNames{
		"global_data",
		"papyrus",
		"init_forms",
		"end_load_game"
	};
	inline constexpr std::array kSaveMetricSchema{
		MetricDescriptor{ "save.overflow_events", Unit::kCount }
	};
	inline constexpr size_t kSaveBurstCapacity{ 256 };

	// Forwards the four integer argument registers; timed calls take no floats.
	using ForwardedCall = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);

	// Owner supplies s_timedOriginals, BeginTimedCall and EndTimedCall.
	template <class Owner, size_t I>
	uint64_t TimedForward(uint64_t a_1, uint64_t a_2, uint64_t a_3, uint64_t a_4)
	{
		const auto start = ReadQpc();
		Owner::BeginTimedCall(I, start);
		const auto result = Owner::s_timedOriginals[I](a_1, a_2, a_3, a_4);
		Owner::EndTimedCall(I, start, ReadQpc());
		return result;
	}

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

	[[nodiscard]] inline Runtime CurrentRuntime() noexcept
	{
		return RELEX::IsRuntimeOG() ? Runtime::kOG :
			RELEX::IsRuntimeAE() ? Runtime::kAE :
			Runtime::kNG;
	}

	[[nodiscard]] constexpr std::string_view RuntimeName(Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kOG ? "OG" :
			a_runtime == Runtime::kNG ? "NG" : "AE";
	}

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
