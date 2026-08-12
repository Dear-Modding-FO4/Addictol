#include <AdProfilerFrameHitch.h>
#include <AdAnimSubGraphRuntime.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>
#include <detours/Detours.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

namespace Addictol
{
	static REX::TOML::I32<> nAdditionalFrameHitchThresholdMs{ "Additional"sv, "nFrameHitchThresholdMs"sv, 50 };

	namespace frameHitchProfilerDetail
	{
		enum class InstallMode
		{
			kFull,
			kTickOnly
		};

		enum class InstallResult
		{
			kInstalled,
			kFailedClean,
			kFailedUnverified
		};

		enum class Phase : std::size_t
		{
			kUpdateIOManager,
			kGeneralUpdate,
			kTreeUpdate,
			kAI,
			kUpdateMessageBox,
			kUpdateTimer,
			kPollControls,
			kUpdateAudio,
			kUpdateCurrentGridCell,
			kUpdateSky,
			kUpdateImageSpace,
			kPostThreadsProcess,
			kCount
		};

		enum class Hook : std::size_t
		{
			kOnIdle,
			kUpdateIOManager,
			kGeneralUpdate,
			kTreeUpdate,
			kAI,
			kUpdateTimer,
			kPollControls,
			kUpdateAudio,
			kUpdateCurrentGridCell,
			kUpdateSky,
			kUpdateImageSpace,
			kPostThreadsProcess,
			kLoadQueuedPriority,
			kClearLoadingTask,
			kCount
		};

		inline constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::kCount);
		inline constexpr std::size_t kHookCount = static_cast<std::size_t>(Hook::kCount);
		inline constexpr std::size_t kSignatureCount = kHookCount + 1;
		inline constexpr std::size_t kSignatureSize = 40;
		inline constexpr std::size_t kIntervalCapacity = 8192;
		inline constexpr std::size_t kHistoryCapacity = 4096;
		inline constexpr std::size_t kHitchCapacity = 128;
		inline constexpr std::size_t kWindowRadius = 4;
		inline constexpr std::size_t kFrameTickCapacity = 8;
		inline constexpr std::uint64_t kReportIntervalMs = 5000;

		using Signature = std::array<std::uint8_t, kSignatureSize>;
		using SignatureSet = std::array<Signature, kSignatureCount>;
		using TGeneric = std::uintptr_t(*)(void*);
		using TFloat = std::uintptr_t(*)(void*, float);
		using TBool = std::uintptr_t(*)(void*, bool);
		using TPriority = std::uintptr_t(*)(void*, std::uint32_t);

		struct Metric
		{
			std::uint64_t ticks{};
			std::uint64_t calls{};
		};

		struct HookActivityCounters
		{
			std::atomic<std::uint64_t> entries{};
			std::atomic<std::uint64_t> rejected{};
			std::atomic<std::uint32_t> firstRejectedThread{};
		};

		struct HookActivitySnapshot
		{
			std::uint64_t entries{};
			std::uint64_t rejected{};
			std::uint32_t firstRejectedThread{};
			struct StallHookOwnership
			{
				std::uintptr_t target{};
				std::array<std::uint8_t, 8> targetBytes{};
				bool targetReadable{};
				bool owned{};
			} ownership;
		};

		struct FrameSample
		{
			std::uint64_t sequence{};
			std::uint64_t frameTicks{};
			std::array<Metric, kPhaseCount> phases{};
			Metric loadQueuedPriority{};
			Metric clearLoadingTask{};
		};

		struct IntervalStats
		{
			std::uint64_t frameCount{};
			std::uint64_t frameTicks{};
			std::uint64_t maxFrameTicks{};
			std::array<std::uint64_t, kIntervalCapacity> samples{};
			std::size_t sampleCount{};
			std::uint64_t droppedSamples{};
			std::array<Metric, kPhaseCount> phases{};
			Metric loadQueuedPriority{};
			Metric clearLoadingTask{};
		};

		struct HitchWindow
		{
			FrameSample hitch{};
			std::array<FrameSample, kWindowRadius * 2 + 1> frames{};
			std::size_t frameCount{};
		};

		struct ReportSnapshot
		{
			IntervalStats interval{};
			std::array<HitchWindow, kHitchCapacity> hitches{};
			std::size_t hitchCount{};
			std::uint64_t droppedHitches{};
			HookActivitySnapshot loadQueuedActivity{};
			HookActivitySnapshot clearLoadingActivity{};
		};

		struct ActiveFrame
		{
			std::array<Metric, kPhaseCount> phases{};
			Metric loadQueuedPriority{};
			Metric clearLoadingTask{};
			std::uint32_t loadQueuedDepth{};
			std::uint32_t clearLoadingDepth{};
			std::uint64_t loadQueuedStart{};
			std::uint64_t clearLoadingStart{};
		};

		struct PollCallSites
		{
			std::array<std::uintptr_t, 2> sites{};
			std::size_t count{};
		};

		struct PollCallSignatures
		{
			std::array<Signature, 2> signatures{};
			std::size_t count{};
		};

		static constexpr std::array<const char*, kPhaseCount> g_phaseNames{
			"UpdateIOManager", "GeneralUpdate", "TreeUpdate", "AI", "UpdateMessageBox", "UpdateTimer",
			"PollControls", "UpdateAudio", "UpdateCurrentGridCell", "UpdateSky", "UpdateImageSpace",
			"PostThreadsProcess"
		};

		static constexpr SignatureSet g_ogSignatures{ {
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0x22, 0x7A, 0xB9, 0x04, 0x48, 0x8B, 0xF9, 0x83, 0xB8, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x97, 0xC0, 0x88, 0x81, 0xD0, 0x01, 0x00 },
			{ 0x40, 0x55, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE5, 0x80, 0x48, 0x8D, 0x4D, 0x00, 0xE8, 0x72, 0xFD, 0xDD, 0x00, 0x83, 0x3D, 0xFB, 0x35, 0xB9, 0x02, 0x02, 0x74, 0x13, 0x48 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x49, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x43, 0x48, 0x8B, 0x0D, 0xEE, 0xAA, 0xD6, 0x04, 0xF3, 0x0F, 0x10, 0x0D, 0x5E, 0x1E, 0xE2, 0x04, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x28 },
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x0A, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x04, 0x33, 0xFF, 0xEB, 0x03, 0x40, 0xB7, 0x01, 0x48, 0x8B, 0x1D, 0xA9, 0x6F, 0xB9, 0x04, 0xE8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x77, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x71, 0x48, 0x8B, 0x0D, 0x5E, 0xA9, 0xD6, 0x04, 0xE8, 0x21, 0xBA, 0x3B, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8, 0xE8 },
			{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x05, 0xE3, 0x4C, 0xA5, 0x02, 0x48, 0x8B, 0xD9, 0x88, 0x05, 0x76, 0x1A, 0xE2, 0x04, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x05, 0xE8, 0x8C, 0x9D, 0xDD, 0x00, 0x48, 0x8D, 0x0D, 0x25 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x0D, 0x59, 0xD2, 0x04, 0xF3, 0x0F, 0x10, 0x0D, 0x71, 0x19, 0xE2, 0x04, 0xE8, 0xC8, 0x20, 0xDF, 0x00, 0xE8, 0x23, 0x22, 0x52, 0x00, 0x48, 0x8B, 0x0D, 0xCC, 0x91, 0xD2, 0x04, 0xE8, 0xF7, 0x73, 0xD1 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x77, 0xD1, 0x74, 0x01, 0xE8, 0xB2, 0x27, 0xF5, 0xFF, 0x48, 0x8B, 0x0D, 0xC3, 0xA5, 0xD6, 0x04, 0x48, 0x85, 0xC9, 0x74, 0x1A, 0xE8, 0xF1, 0x6D, 0x0C, 0x00, 0x84, 0xC0, 0x74, 0x11, 0xF3, 0x0F, 0x10, 0x05, 0x0D },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x33, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x2D, 0xE8, 0x98, 0x74, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x24, 0x48, 0x8B, 0x15, 0x25, 0xA3, 0xD6, 0x04, 0x48, 0x8B, 0x0D, 0x1E, 0xA2 },
			{ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0D, 0x80, 0xB9, 0xD1, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x13, 0x02, 0x00, 0x00, 0x80, 0x79, 0x2A, 0x00, 0x0F, 0x85, 0x09, 0x02, 0x00, 0x00, 0x48 },
			{ 0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x05, 0xA8, 0x7D, 0x9E, 0x05, 0xF3, 0x0F, 0x10, 0x05, 0xC0, 0xFB, 0x9D, 0x02, 0xF3, 0x0F, 0x10, 0x0D, 0xD0, 0xFB, 0x9D, 0x02, 0x0F, 0xB6, 0xEA, 0xF3, 0x0F, 0x11, 0x80, 0xA8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xD7, 0xA7, 0x7A, 0xFF, 0xF3, 0x0F, 0x10, 0x0D, 0x5F, 0x40, 0xA5, 0x02, 0x48, 0x8B, 0x0D, 0x78, 0xBF, 0xD8, 0x04, 0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x0D, 0x1A, 0x0C, 0xF1, 0x01, 0x48, 0x83 },
			{ 0x40, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x83, 0xB9, 0x24, 0x14, 0x00, 0x00, 0x07, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x0F, 0x84, 0xDB, 0x02, 0x00, 0x00, 0x83, 0x3D, 0xD0, 0x93, 0xC2, 0x01, 0x02, 0x74, 0x13, 0x48, 0x8D, 0x15, 0xC7, 0x93, 0xC2 },
			{ 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x74, 0x59, 0x83, 0xB9, 0x10, 0x03, 0x00, 0x00, 0x03, 0x74, 0x05, 0xE8, 0x30, 0xCB, 0xFF, 0xFF, 0x48, 0x8B, 0x47, 0x10, 0x8B, 0x88, 0x10, 0x03 },
			{ 0xE8, 0x20, 0xC4, 0x07, 0x00, 0x48, 0x8B, 0x0D, 0x31, 0x6A, 0xDE, 0x04, 0x48, 0x83, 0xC4, 0x28, 0xE9, 0x90, 0xBF, 0x07, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x40, 0x53, 0x48 }
		} };

		static constexpr SignatureSet g_ngSignatures{ {
			{ 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x01, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xC8, 0x48, 0x8D, 0xA8, 0xC8, 0xFE },
			{ 0x40, 0x55, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE5, 0x80, 0x48, 0x8D, 0x4D, 0x00, 0xE8, 0xD2, 0x7F, 0x99, 0x00, 0x83, 0x3D, 0x3B, 0x40, 0x2B, 0x02, 0x02, 0x74, 0x13, 0x48 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x49, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x43, 0x48, 0x8B, 0x0D, 0x3E, 0xBE, 0x48, 0x02, 0xF3, 0x0F, 0x10, 0x0D, 0xFE, 0x01, 0x54, 0x02, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x28 },
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x0A, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x04, 0x33, 0xFF, 0xEB, 0x03, 0x40, 0xB7, 0x01, 0x48, 0x8B, 0x1D, 0x61, 0x75, 0x2B, 0x02, 0xE8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x77, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x71, 0x48, 0x8B, 0x0D, 0xC6, 0xBC, 0x48, 0x02, 0xE8, 0x71, 0xA9, 0x6C, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8, 0xE8 },
			{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x05, 0x13, 0x1D, 0x11, 0x02, 0x48, 0x8B, 0xD9, 0x88, 0x05, 0x36, 0xFE, 0x53, 0x02, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x05, 0xE8, 0x5C, 0x27, 0x99, 0x00, 0x48, 0x8D, 0x0D, 0xE5 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xF3, 0x0F, 0x10, 0x0D, 0x38, 0xFD, 0x53, 0x02, 0x48, 0x8B, 0x0D, 0x95, 0x1F, 0x44, 0x02, 0xE8, 0xA8, 0x8C, 0x9A, 0x00, 0xE8, 0x93, 0x1F, 0x40, 0x00, 0x48, 0x8B, 0x0D, 0x1C, 0x4D, 0x44, 0x02, 0xE8, 0x07, 0xC3, 0xDD },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x07, 0x45, 0x12, 0x01, 0xE8, 0x02, 0xED, 0xF7, 0xFF, 0x48, 0x8B, 0x0D, 0x3B, 0xB9, 0x48, 0x02, 0x48, 0x85, 0xC9, 0x74, 0x1A, 0xE8, 0xB1, 0xCD, 0x09, 0x00, 0x84, 0xC0, 0x74, 0x11, 0xF3, 0x0F, 0x10, 0x05, 0x3D },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x33, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x2D, 0xE8, 0xE8, 0xC8, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x24, 0x48, 0x8B, 0x15, 0x2D, 0xB6, 0x48, 0x02, 0x45, 0x33, 0xC9, 0x48, 0x8B },
			{ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0D, 0x80, 0xB9, 0xD1, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xF9, 0x01, 0x00, 0x00, 0x80, 0x79, 0x2A, 0x00, 0x0F, 0x85, 0xEF, 0x01, 0x00, 0x00, 0x48 },
			{ 0x40, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x70, 0x32, 0x0F, 0x03, 0x0F, 0xB6, 0xEA, 0xF3, 0x0F, 0x10, 0x0D, 0x2D, 0xBF, 0x0E, 0x02, 0xF3, 0x0F, 0x10, 0x05, 0x0D, 0xBF, 0x0E, 0x02, 0xF3, 0x0F, 0x11, 0x80, 0xA8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xA7, 0x0C, 0x9B, 0xFF, 0xF3, 0x0F, 0x10, 0x0D, 0x9F, 0x0F, 0x11, 0x02, 0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x0D, 0x91, 0x62, 0x6C, 0x01, 0x48, 0x8B, 0x0D, 0xA2, 0xD3, 0x4A, 0x02, 0x48, 0x83 },
			{ 0x40, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x83, 0xB9, 0xA4, 0x14, 0x00, 0x00, 0x07, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x0F, 0x84, 0xDC, 0x02, 0x00, 0x00, 0x83, 0x3D, 0x50, 0xE7, 0x7C, 0x01, 0x02, 0x74, 0x13, 0x48, 0x8D, 0x15, 0x47, 0xE7, 0x7C },
			{ 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x86, 0x00, 0x00, 0x00, 0x83, 0xB9, 0x10, 0x03, 0x00, 0x00, 0x03, 0x48, 0x8B, 0xC1, 0x74, 0x09, 0xE8, 0x99, 0xE1, 0xFF, 0xFF, 0x48 },
			{ 0xE8, 0xA0, 0x90, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0x41, 0x4A, 0x4E, 0x02, 0x48, 0x83, 0xC4, 0x28, 0xE9, 0x30, 0x8C, 0x05, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x40, 0x53, 0x48 }
		} };

		static constexpr SignatureSet g_aeSignatures{ {
			{ 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x01, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xC8, 0x48, 0x8D, 0xA8, 0xC8, 0xFE },
			{ 0x40, 0x55, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE5, 0x80, 0x48, 0x8D, 0x4D, 0x00, 0xE8, 0xC2, 0xC6, 0xA2, 0x00, 0x83, 0x3D, 0x5B, 0xAD, 0x22, 0x03, 0x02, 0x74, 0x13, 0x48 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x49, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x43, 0x48, 0x8B, 0x0D, 0x4E, 0xDC, 0x69, 0x02, 0xF3, 0x0F, 0x10, 0x0D, 0x4E, 0xFD, 0x75, 0x02, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x28 },
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x0A, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x04, 0x33, 0xFF, 0xEB, 0x03, 0x40, 0xB7, 0x01, 0x48, 0x8B, 0x1D, 0xE1, 0x91, 0x4A, 0x02, 0xE8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x77, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x71, 0x48, 0x8B, 0x0D, 0xAE, 0xD9, 0x69, 0x02, 0xE8, 0x11, 0x8D, 0x69, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8, 0xE8 },
			{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x05, 0xC3, 0x2A, 0x2F, 0x02, 0x48, 0x8B, 0xD9, 0x88, 0x05, 0x96, 0xF9, 0x75, 0x02, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x05, 0xE8, 0xDC, 0x6D, 0xA2, 0x00, 0x48, 0x8D, 0x0D, 0x35 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xF3, 0x0F, 0x10, 0x0D, 0x88, 0xF8, 0x75, 0x02, 0x48, 0x8B, 0x0D, 0x45, 0xA8, 0x22, 0x03, 0xE8, 0xE8, 0xD6, 0xA3, 0x00, 0xE8, 0xD3, 0x23, 0x40, 0x00, 0x48, 0x8B, 0x0D, 0xDC, 0x69, 0x63, 0x02, 0xE8, 0x97, 0xA2, 0xDA },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xE7, 0x9E, 0x1B, 0x01, 0xE8, 0xF2, 0xCE, 0xF6, 0xFF, 0x48, 0x8B, 0x0D, 0x4B, 0xD7, 0x69, 0x02, 0x48, 0x85, 0xC9, 0x74, 0x1A, 0xE8, 0xC1, 0xD1, 0x09, 0x00, 0x84, 0xC0, 0x74, 0x11, 0xF3, 0x0F, 0x10, 0x05, 0xED },
			{ 0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x75, 0x33, 0x80, 0x79, 0x2A, 0x00, 0x75, 0x2D, 0xE8, 0xD8, 0xCC, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x24, 0x48, 0x8B, 0x15, 0x3D, 0xD4, 0x69, 0x02, 0x45, 0x33, 0xC9, 0x48, 0x8B },
			{ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x80, 0xB9, 0xD0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0D, 0x80, 0xB9, 0xD1, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xF9, 0x01, 0x00, 0x00, 0x80, 0x79, 0x2A, 0x00, 0x0F, 0x85, 0xEF, 0x01, 0x00, 0x00, 0x48 },
			{ 0x40, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x10, 0x2A, 0x21, 0x03, 0x0F, 0xB6, 0xEA, 0xF3, 0x0F, 0x10, 0x0D, 0x8D, 0xC9, 0x2C, 0x02, 0xF3, 0x0F, 0x10, 0x05, 0x6D, 0xC9, 0x2C, 0x02, 0xF3, 0x0F, 0x11, 0x80, 0xA8 },
			{ 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x07, 0xF4, 0x97, 0xFF, 0xF3, 0x0F, 0x10, 0x0D, 0x4F, 0x1D, 0x2F, 0x02, 0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x0D, 0x69, 0xA9, 0xCE, 0x01, 0x48, 0x8B, 0x0D, 0x52, 0xF4, 0x6B, 0x02, 0x48, 0x83 },
			{ 0x40, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x83, 0xB9, 0xA4, 0x14, 0x00, 0x00, 0x07, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x0F, 0x84, 0xDC, 0x02, 0x00, 0x00, 0x83, 0x3D, 0x20, 0xFF, 0x6A, 0x02, 0x02, 0x74, 0x13, 0x48, 0x8D, 0x15, 0x17, 0xFF, 0x6A },
			{ 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x86, 0x00, 0x00, 0x00, 0x83, 0xB9, 0x10, 0x03, 0x00, 0x00, 0x03, 0x48, 0x8B, 0xC1, 0x74, 0x09, 0xE8, 0x99, 0xE1, 0xFF, 0xFF, 0x48 },
			{ 0xE8, 0x10, 0xF3, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0x79, 0xC9, 0x6F, 0x02, 0x48, 0x83, 0xC4, 0x28, 0xE9, 0xA0, 0xEE, 0x05, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x40, 0x53, 0x48 }
		} };

		static constexpr PollCallSignatures g_ogPollCallSignatures{
			{ {
				{ 0xE8, 0xA7, 0xB9, 0x07, 0x00, 0x48, 0x8B, 0x0D, 0xC8, 0x5E, 0xDE, 0x04, 0xE8, 0x8B, 0xBA, 0x07, 0x00, 0x48, 0x8B, 0x05, 0xCC, 0x5F, 0xDE, 0x04, 0x48, 0x8B, 0x88, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xF1, 0x00, 0x00, 0x00 },
				{}
			} },
			1
		};

		static constexpr PollCallSignatures g_ngPollCallSignatures{
			{ {
				{ 0xE8, 0xB7, 0x85, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0x68, 0x3E, 0x4E, 0x02, 0xE8, 0x9B, 0x86, 0x05, 0x00, 0x48, 0x8B, 0x05, 0x54, 0x3F, 0x4E, 0x02, 0x48, 0x8B, 0x88, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00 },
				{ 0xE8, 0xFB, 0x82, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0xAC, 0x3B, 0x4E, 0x02, 0xE8, 0xDF, 0x83, 0x05, 0x00, 0x48, 0x8B, 0x05, 0x98, 0x3C, 0x4E, 0x02, 0x48, 0x8B, 0x88, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00 }
			} },
			2
		};

		static constexpr PollCallSignatures g_aePollCallSignatures{
			{ {
				{ 0xE8, 0x27, 0xE8, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0xA0, 0xBD, 0x6F, 0x02, 0xE8, 0x0B, 0xE9, 0x05, 0x00, 0x48, 0x8B, 0x05, 0xD4, 0xBF, 0x6F, 0x02, 0x48, 0x8B, 0x88, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00 },
				{ 0xE8, 0x6B, 0xE5, 0x05, 0x00, 0x48, 0x8B, 0x0D, 0xE4, 0xBA, 0x6F, 0x02, 0xE8, 0x4F, 0xE6, 0x05, 0x00, 0x48, 0x8B, 0x05, 0x18, 0xBD, 0x6F, 0x02, 0x48, 0x8B, 0x88, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00 }
			} },
			2
		};

		static std::array<std::uintptr_t, kHookCount> g_originals;
		static std::array<FrameTickCallback, kFrameTickCapacity> g_frameTickCallbacks;
		static std::atomic<std::size_t> g_frameTickCount;
		static std::array<std::uint8_t, 5> g_messageCallOriginal;
		static std::array<std::array<std::uint8_t, 5>, 2> g_pollCallOriginals;
		static std::size_t g_pollCallsInstalled;
		static TGeneric g_messageOriginal;
		static std::uint64_t g_frequency;
		static std::uint64_t g_thresholdTicks;
		static std::uint64_t g_nextReportTicks;
		static std::uint64_t g_sequence;
		static IntervalStats g_interval;
		static std::array<FrameSample, kHistoryCapacity> g_history;
		static std::array<std::uint64_t, kHitchCapacity> g_hitchSequences;
		static std::size_t g_hitchCount;
		static std::uint64_t g_droppedHitches;
		static ReportSnapshot g_report;
		static std::atomic<bool> g_reportBusy;
		static std::atomic<std::uint32_t> g_frameThreadId;
		static HookActivityCounters g_loadQueuedActivity;
		static HookActivityCounters g_clearLoadingActivity;
		// Hooks intentionally live until process exit because module teardown is not reachable.
		static bool g_installed;
		static InstallMode g_installMode{ InstallMode::kFull };
		static thread_local std::uint32_t g_frameDepth;
		static thread_local bool g_phaseFrameActive;
		static thread_local ActiveFrame g_activeFrame;
		static thread_local std::array<std::uint32_t, kPhaseCount> g_phaseDepth;
		static thread_local std::array<std::uint64_t, kPhaseCount> g_phaseStart;

		[[nodiscard]] static HookActivitySnapshot::StallHookOwnership InspectStallHook(Hook a_hook) noexcept;

		[[nodiscard]] static bool RegisterFrameTick(FrameTickCallback a_callback) noexcept
		{
			if (!a_callback)
			{
				REX::WARN("Frame Hitch profiler: rejected null frame-tick callback."sv);
				return false;
			}
			// Subscribers must register before installation so hook mode selection sees them.
			if (g_installed)
			{
				REX::WARN("Frame Hitch profiler: rejected frame-tick callback registered after installation."sv);
				return false;
			}
			const auto count = g_frameTickCount.load(std::memory_order_acquire);
			if (count >= g_frameTickCallbacks.size())
			{
				REX::WARN("Frame Hitch profiler: frame-tick subscriber registry is full."sv);
				return false;
			}
			g_frameTickCallbacks[count] = a_callback;
			g_frameTickCount.store(count + 1, std::memory_order_release);
			return true;
		}

		[[nodiscard]] static bool HasFrameTickSubscribers() noexcept
		{
			return g_frameTickCount.load(std::memory_order_acquire) != 0;
		}

		[[nodiscard]] static bool IsFrameThread() noexcept
		{
			const auto current = static_cast<std::uint32_t>(GetCurrentThreadId());
			auto owner = g_frameThreadId.load(std::memory_order_acquire);
			// A failed exchange stores the winner into owner, so a losing thread falls through to a correct compare.
			if (!owner && g_frameThreadId.compare_exchange_strong(
				owner, current, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				// Logged once so a mis-claimed frame thread is diagnosable; it would otherwise record nothing silently.
				REX::INFO("Frame Hitch profiler: frame thread claimed by {}."sv, current);
				return true;
			}
			return owner == current;
		}

		[[nodiscard]] static bool IsRelocatedHook(std::size_t a_index) noexcept
		{
			return a_index == static_cast<std::size_t>(Hook::kUpdateAudio) ||
				a_index == static_cast<std::size_t>(Hook::kPostThreadsProcess);
		}

		[[nodiscard]] static constexpr std::size_t RelocatedLength() noexcept
		{
			return 9;
		}

		[[nodiscard]] static std::size_t PatchedLength(std::size_t a_index) noexcept
		{
			switch (static_cast<Hook>(a_index))
			{
			case Hook::kOnIdle:
				return RELEX::IsRuntimeOG() ? 5 : 7;
			case Hook::kUpdateIOManager:
				return 9;
			case Hook::kGeneralUpdate:
			case Hook::kAI:
			case Hook::kUpdateCurrentGridCell:
				return 11;
			case Hook::kTreeUpdate:
			case Hook::kUpdateImageSpace:
				return 5;
			case Hook::kUpdateTimer:
			case Hook::kClearLoadingTask:
				return 6;
			case Hook::kUpdateAudio:
			case Hook::kPostThreadsProcess:
				return RelocatedLength();
			case Hook::kUpdateSky:
			case Hook::kLoadQueuedPriority:
				return 7;
			case Hook::kPollControls:
			case Hook::kCount:
				return 0;
			}
			return 0;
		}

		[[nodiscard]] static std::uint64_t Counter() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		[[nodiscard]] static double Milliseconds(std::uint64_t a_ticks) noexcept
		{
			return g_frequency ?
				static_cast<double>(a_ticks) * 1000.0 / static_cast<double>(g_frequency) :
				0.0;
		}

		[[nodiscard]] static std::array<std::uintptr_t, kSignatureCount> ResolveTargets() noexcept
		{
			return {
				REL::ID{ 633524, 2228917, 2228917 }.address(),
				REL::ID{ 868578, 2228918, 2228918 }.address(),
				REL::ID{ 775786, 2228919, 2228919 }.address(),
				REL::ID{ 682707, 2228920, 2228920 }.address(),
				REL::ID{ 1293756, 2228921, 2228921 }.address(),
				REL::ID{ 1252654, 2228926, 2228926 }.address(),
				REL::ID{ 1065935, 2228927, 2228927 }.address(),
				REL::ID{ 534189, 2228928, 2228928 }.address(),
				REL::ID{ 1117916, 2228930, 2228930 }.address(),
				REL::ID{ 586224, 2228931, 2228931 }.address(),
				REL::ID{ 1169995, 2228932, 2228932 }.address(),
				REL::ID{ 704220, 2228933, 2228933 }.address(),
				REL::ID{ 1310667, 2275043, 2275043 }.address(),
				REL::ID{ 1168526, 2214637, 2214637 }.address(),
				REL::Relocation<>{ REL::ID{ 773556, 2227569, 2227569 }, REL::Offset{ 0xB } }.address()
			};
		}

		[[nodiscard]] static const SignatureSet& RuntimeSignatures() noexcept
		{
			if (RELEX::IsRuntimeOG())
				return g_ogSignatures;
			if (RELEX::IsRuntimeNG())
				return g_ngSignatures;
			return g_aeSignatures;
		}

		[[nodiscard]] static PollCallSites ResolvePollCallSites() noexcept
		{
			if (RELEX::IsRuntimeOG())
			{
				return {
					{ REL::Relocation<>{ REL::ID{ 556439 }, REL::Offset{ 0xE4 } }.address(), 0 },
					1
				};
			}
			return {
				{
					REL::Relocation<>{ REL::ID{ 2227608, 2227608, 2227608 }, REL::Offset{ 0xE4 } }.address(),
					REL::Relocation<>{ REL::ID{ 2227611, 2227611, 2227611 }, REL::Offset{ 0xF0 } }.address()
				},
				2
			};
		}

		[[nodiscard]] static const PollCallSignatures& RuntimePollCallSignatures() noexcept
		{
			if (RELEX::IsRuntimeOG())
				return g_ogPollCallSignatures;
			if (RELEX::IsRuntimeNG())
				return g_ngPollCallSignatures;
			return g_aePollCallSignatures;
		}

		[[nodiscard]] static bool ValidateTargets(InstallMode a_mode) noexcept
		{
			const auto targets = ResolveTargets();
			const auto& signatures = RuntimeSignatures();
			const auto targetCount = a_mode == InstallMode::kFull ? targets.size() : 1;
			for (std::size_t index = 0; index < targetCount; ++index)
			{
				if (!AnimSubGraphRuntime::ValidateUniqueSignature(targets[index], signatures[index]))
				{
					REX::WARN("Frame Hitch profiler: target {} at {:X} failed exact unique-signature validation; installing nothing."sv,
						index, targets[index]);
					return false;
				}
			}
			if (a_mode == InstallMode::kTickOnly)
				return true;

			const auto messageCall = targets.back();
			if (*reinterpret_cast<const std::uint8_t*>(messageCall) != 0xE8)
			{
				REX::WARN("Frame Hitch profiler: message-box call site at {:X} is not E8; installing nothing."sv, messageCall);
				return false;
			}
			const auto displacement = *reinterpret_cast<const std::int32_t*>(messageCall + 1);
			const auto destination = messageCall + 5 + static_cast<std::intptr_t>(displacement);
			const auto expected = REL::ID{ 813902, 2718226, 2718226 }.address();
			if (destination != expected)
			{
				REX::WARN("Frame Hitch profiler: message-box call site targets {:X}, expected {:X}; installing nothing."sv,
					destination, expected);
				return false;
			}

			const auto pollTarget = targets[static_cast<std::size_t>(Hook::kPollControls)];
			const auto pollCalls = ResolvePollCallSites();
			const auto& pollSignatures = RuntimePollCallSignatures();
			for (std::size_t index = 0; index < pollCalls.count; ++index)
			{
				const auto site = pollCalls.sites[index];
				if (!AnimSubGraphRuntime::ValidateUniqueSignature(site, pollSignatures.signatures[index]) ||
					*reinterpret_cast<const std::uint8_t*>(site) != 0xE8)
				{
					REX::WARN("Frame Hitch profiler: PollControls call site {} at {:X} failed validation; installing nothing."sv,
						index, site);
					return false;
				}
				const auto pollDisplacement = *reinterpret_cast<const std::int32_t*>(site + 1);
				const auto pollDestination = site + 5 + static_cast<std::intptr_t>(pollDisplacement);
				if (pollDestination != pollTarget)
				{
					REX::WARN("Frame Hitch profiler: PollControls call site {} targets {:X}, expected {:X}; installing nothing."sv,
						index, pollDestination, pollTarget);
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] static bool BeginPhase(Phase a_phase) noexcept
		{
			if (!g_phaseFrameActive)
				return false;
			const auto index = static_cast<std::size_t>(a_phase);
			++g_activeFrame.phases[index].calls;
			if (g_phaseDepth[index]++ == 0)
				g_phaseStart[index] = Counter();
			return true;
		}

		static void EndPhase(Phase a_phase, bool a_active) noexcept
		{
			if (!a_active)
				return;
			const auto index = static_cast<std::size_t>(a_phase);
			if (--g_phaseDepth[index] == 0)
				g_activeFrame.phases[index].ticks += Counter() - g_phaseStart[index];
		}

		[[nodiscard]] static bool BeginStall(
			HookActivityCounters& a_activity, Metric& a_metric,
			std::uint32_t& a_depth, std::uint64_t& a_start) noexcept
		{
			a_activity.entries.fetch_add(1, std::memory_order_relaxed);
			if (g_frameDepth != 1)
			{
				auto expected = std::uint32_t{ 0 };
				a_activity.firstRejectedThread.compare_exchange_strong(
					expected, static_cast<std::uint32_t>(GetCurrentThreadId()),
					std::memory_order_relaxed, std::memory_order_relaxed);
				a_activity.rejected.fetch_add(1, std::memory_order_release);
				return false;
			}
			++a_metric.calls;
			if (a_depth++ == 0)
				a_start = Counter();
			return true;
		}

		static void EndStall(Metric& a_metric, std::uint32_t& a_depth, std::uint64_t a_start, bool a_active) noexcept
		{
			if (a_active && --a_depth == 0)
				a_metric.ticks += Counter() - a_start;
		}

		static void Accumulate(Metric& a_target, const Metric& a_value) noexcept
		{
			a_target.ticks += a_value.ticks;
			a_target.calls += a_value.calls;
		}

		[[nodiscard]] static std::uint64_t Percentile(
			const std::array<std::uint64_t, kIntervalCapacity>& a_samples,
			std::size_t a_count, std::uint32_t a_percent) noexcept
		{
			if (!a_count)
				return 0;
			const auto index = std::min(
				a_count - 1,
				(static_cast<std::size_t>(a_percent) * a_count + 99) / 100 - 1);
			return a_samples[index];
		}

		[[nodiscard]] static FrameHitchFrameProfileEntry MakeFrameProfileEntry(const FrameSample& a_sample) noexcept
		{
			FrameHitchFrameProfileEntry entry;
			entry.sequence = a_sample.sequence;
			entry.frameMs = Milliseconds(a_sample.frameTicks);
			entry.loadQueuedPriority = {
				Milliseconds(a_sample.loadQueuedPriority.ticks), a_sample.loadQueuedPriority.calls
			};
			entry.clearLoadingTask = {
				Milliseconds(a_sample.clearLoadingTask.ticks), a_sample.clearLoadingTask.calls
			};
			for (std::size_t index = 0; index < a_sample.phases.size(); ++index)
				entry.phases[index] = {
					Milliseconds(a_sample.phases[index].ticks), a_sample.phases[index].calls
				};
			return entry;
		}

		static void LogOwnershipWarning(
			std::string_view a_name,
			const HookActivitySnapshot::StallHookOwnership& a_ownership) noexcept
		{
			if (a_ownership.targetReadable)
				REX::WARN("[Profiler/FrameHitch] hook {} ownership check failed at {:X}; target bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}."sv,
					a_name, a_ownership.target,
					static_cast<unsigned>(a_ownership.targetBytes[0]),
					static_cast<unsigned>(a_ownership.targetBytes[1]),
					static_cast<unsigned>(a_ownership.targetBytes[2]),
					static_cast<unsigned>(a_ownership.targetBytes[3]),
					static_cast<unsigned>(a_ownership.targetBytes[4]),
					static_cast<unsigned>(a_ownership.targetBytes[5]),
					static_cast<unsigned>(a_ownership.targetBytes[6]),
					static_cast<unsigned>(a_ownership.targetBytes[7]));
			else
				REX::WARN("[Profiler/FrameHitch] hook {} ownership check failed because target {:X} is unreadable."sv,
					a_name, a_ownership.target);
		}

		static void LogHookActivity(
			std::string_view a_name, const HookActivitySnapshot& a_activity) noexcept
		{
			const auto accepted = a_activity.entries - a_activity.rejected;
			REX::INFO("[Profiler/FrameHitch] hook liveness cumulative {}: raw {}, accepted {}, rejected {}, first rejected thread {}, detour owned {}."sv,
				a_name, a_activity.entries, accepted, a_activity.rejected,
				a_activity.firstRejectedThread, a_activity.ownership.owned);
			if (!a_activity.ownership.owned)
				LogOwnershipWarning(a_name, a_activity.ownership);
		}

		static void CALLBACK ReportCallback(
			[[maybe_unused]] PTP_CALLBACK_INSTANCE a_instance,
			[[maybe_unused]] void* a_context) noexcept
		{
			auto& stats = g_report.interval;
			std::sort(stats.samples.begin(), stats.samples.begin() + stats.sampleCount);
			const auto mean = stats.frameCount ?
				Milliseconds(stats.frameTicks) / static_cast<double>(stats.frameCount) :
				0.0;
			REX::INFO("[Profiler/FrameHitch] summary: frames {}, mean {:.3f} ms, P95 {:.3f} ms, P99 {:.3f} ms, max {:.3f} ms; percentile samples {}, dropped {}."sv,
				stats.frameCount, mean,
				Milliseconds(Percentile(stats.samples, stats.sampleCount, 95)),
				Milliseconds(Percentile(stats.samples, stats.sampleCount, 99)),
				Milliseconds(stats.maxFrameTicks), stats.sampleCount, stats.droppedSamples);
			REX::INFO("[Profiler/FrameHitch] summary stalls: LoadQueuedPriority {:.3f} ms/{} calls; ClearLoadingTask {:.3f} ms/{} calls."sv,
				Milliseconds(stats.loadQueuedPriority.ticks), stats.loadQueuedPriority.calls,
				Milliseconds(stats.clearLoadingTask.ticks), stats.clearLoadingTask.calls);
			LogHookActivity("LoadQueuedPriority"sv, g_report.loadQueuedActivity);
			LogHookActivity("ClearLoadingTask"sv, g_report.clearLoadingActivity);
			for (std::size_t index = 0; index < kPhaseCount; ++index)
			{
				const auto& metric = stats.phases[index];
				if (metric.calls)
					REX::INFO("[Profiler/FrameHitch] summary phase {}: {:.3f} ms/{} calls."sv,
						g_phaseNames[index], Milliseconds(metric.ticks), metric.calls);
			}
			for (std::size_t hitchIndex = 0; hitchIndex < g_report.hitchCount; ++hitchIndex)
			{
				const auto& window = g_report.hitches[hitchIndex];
				const auto& hitch = window.hitch;
				REX::INFO("[Profiler/FrameHitch] hitch frame {}: {:.3f} ms; LoadQueuedPriority {:.3f} ms/{}; ClearLoadingTask {:.3f} ms/{}."sv,
					hitch.sequence, Milliseconds(hitch.frameTicks),
					Milliseconds(hitch.loadQueuedPriority.ticks), hitch.loadQueuedPriority.calls,
					Milliseconds(hitch.clearLoadingTask.ticks), hitch.clearLoadingTask.calls);
				for (std::size_t phaseIndex = 0; phaseIndex < kPhaseCount; ++phaseIndex)
				{
					const auto& metric = hitch.phases[phaseIndex];
					if (metric.calls)
						REX::INFO("[Profiler/FrameHitch] hitch frame {} phase {}: {:.3f} ms/{} calls."sv,
							hitch.sequence, g_phaseNames[phaseIndex], Milliseconds(metric.ticks), metric.calls);
				}
				for (std::size_t frameIndex = 0; frameIndex < window.frameCount; ++frameIndex)
				{
					const auto& frame = window.frames[frameIndex];
					REX::INFO("[Profiler/FrameHitch] window frame {}: {:.3f} ms, stalls {:.3f} ms/{} calls."sv,
						frame.sequence, Milliseconds(frame.frameTicks),
						Milliseconds(frame.loadQueuedPriority.ticks + frame.clearLoadingTask.ticks),
						frame.loadQueuedPriority.calls + frame.clearLoadingTask.calls);
				}
			}
			if (g_report.droppedHitches)
				REX::WARN("[Profiler/FrameHitch] {} hitch records exceeded the interval buffer."sv, g_report.droppedHitches);

			FrameHitchProfileEntry entry;
			entry.frameCount = stats.frameCount;
			entry.meanMs = mean;
			entry.p95Ms = Milliseconds(Percentile(stats.samples, stats.sampleCount, 95));
			entry.p99Ms = Milliseconds(Percentile(stats.samples, stats.sampleCount, 99));
			entry.maxMs = Milliseconds(stats.maxFrameTicks);
			entry.percentileSamples = stats.sampleCount;
			entry.droppedSamples = stats.droppedSamples;
			entry.loadQueuedPriority = {
				Milliseconds(stats.loadQueuedPriority.ticks), stats.loadQueuedPriority.calls
			};
			entry.clearLoadingTask = {
				Milliseconds(stats.clearLoadingTask.ticks), stats.clearLoadingTask.calls
			};
			for (std::size_t index = 0; index < stats.phases.size(); ++index)
				entry.phases[index] = {
					Milliseconds(stats.phases[index].ticks), stats.phases[index].calls
				};
			entry.hitches.reserve(g_report.hitchCount);
			for (std::size_t hitchIndex = 0; hitchIndex < g_report.hitchCount; ++hitchIndex)
			{
				const auto& source = g_report.hitches[hitchIndex];
				auto& window = entry.hitches.emplace_back();
				window.hitch = MakeFrameProfileEntry(source.hitch);
				window.frames.reserve(source.frameCount);
				for (std::size_t frameIndex = 0; frameIndex < source.frameCount; ++frameIndex)
					window.frames.push_back(MakeFrameProfileEntry(source.frames[frameIndex]));
			}
			entry.droppedHitches = g_report.droppedHitches;
			ProfilerCore::RecordFrameHitchRuntimeInterval(std::move(entry));
			g_reportBusy.store(false, std::memory_order_release);
		}

		static void QueueReport(std::uint64_t a_now) noexcept
		{
			if (a_now < g_nextReportTicks ||
				g_reportBusy.exchange(true, std::memory_order_acq_rel))
				return;

			g_report = {};
			g_report.interval = g_interval;
			g_report.droppedHitches = g_droppedHitches;
			const auto loadQueuedRejected = g_loadQueuedActivity.rejected.load(std::memory_order_acquire);
			const auto loadQueuedEntries = g_loadQueuedActivity.entries.load(std::memory_order_acquire);
			const auto clearLoadingRejected = g_clearLoadingActivity.rejected.load(std::memory_order_acquire);
			const auto clearLoadingEntries = g_clearLoadingActivity.entries.load(std::memory_order_acquire);
			g_report.loadQueuedActivity = {
				loadQueuedEntries,
				loadQueuedRejected,
				g_loadQueuedActivity.firstRejectedThread.load(std::memory_order_relaxed),
				InspectStallHook(Hook::kLoadQueuedPriority)
			};
			g_report.clearLoadingActivity = {
				clearLoadingEntries,
				clearLoadingRejected,
				g_clearLoadingActivity.firstRejectedThread.load(std::memory_order_relaxed),
				InspectStallHook(Hook::kClearLoadingTask)
			};
			std::size_t readyHitches = 0;
			for (; readyHitches < g_hitchCount; ++readyHitches)
			{
				const auto sequence = g_hitchSequences[readyHitches];
				if (sequence + kWindowRadius > g_sequence)
					break;
				const auto& hitch = g_history[sequence % kHistoryCapacity];
				if (hitch.sequence != sequence)
					continue;
				auto& window = g_report.hitches[g_report.hitchCount++];
				window.hitch = hitch;
				const auto first = sequence > kWindowRadius ? sequence - kWindowRadius : 1;
				const auto last = sequence + kWindowRadius;
				for (auto current = first; current <= last; ++current)
				{
					const auto& sample = g_history[current % kHistoryCapacity];
					if (sample.sequence == current)
						window.frames[window.frameCount++] = sample;
				}
			}

			if (!TrySubmitThreadpoolCallback(ReportCallback, nullptr, nullptr))
			{
				g_reportBusy.store(false, std::memory_order_release);
				g_nextReportTicks = a_now + g_frequency;
				return;
			}

			g_interval = {};
			for (std::size_t index = readyHitches; index < g_hitchCount; ++index)
				g_hitchSequences[index - readyHitches] = g_hitchSequences[index];
			g_hitchCount -= readyHitches;
			g_droppedHitches = 0;
			g_nextReportTicks = a_now + g_frequency * kReportIntervalMs / 1000;
		}

		static void FinishFrame(std::uint64_t a_ticks, std::uint64_t a_sequence) noexcept
		{
			FrameSample sample{};
			sample.sequence = a_sequence;
			sample.frameTicks = a_ticks;
			sample.phases = g_activeFrame.phases;
			sample.loadQueuedPriority = g_activeFrame.loadQueuedPriority;
			sample.clearLoadingTask = g_activeFrame.clearLoadingTask;
			g_history[sample.sequence % kHistoryCapacity] = sample;

			++g_interval.frameCount;
			g_interval.frameTicks += sample.frameTicks;
			g_interval.maxFrameTicks = std::max(g_interval.maxFrameTicks, sample.frameTicks);
			if (g_interval.sampleCount < g_interval.samples.size())
				g_interval.samples[g_interval.sampleCount++] = sample.frameTicks;
			else
				++g_interval.droppedSamples;
			for (std::size_t index = 0; index < kPhaseCount; ++index)
				Accumulate(g_interval.phases[index], sample.phases[index]);
			Accumulate(g_interval.loadQueuedPriority, sample.loadQueuedPriority);
			Accumulate(g_interval.clearLoadingTask, sample.clearLoadingTask);

			if (sample.frameTicks >= g_thresholdTicks)
			{
				if (g_hitchCount < g_hitchSequences.size())
					g_hitchSequences[g_hitchCount++] = sample.sequence;
				else
					++g_droppedHitches;
			}
			QueueReport(Counter());
		}

		static void DispatchFrameTicks(const FrameTick& a_tick) noexcept
		{
			const auto count = g_frameTickCount.load(std::memory_order_acquire);
			for (std::size_t index = 0; index < count; ++index)
				g_frameTickCallbacks[index](a_tick);
		}

		static std::uintptr_t HKOnIdle(void* a_this) noexcept
		{
			const auto original = reinterpret_cast<TGeneric>(g_originals[static_cast<std::size_t>(Hook::kOnIdle)]);
			if (!IsFrameThread())
				return original(a_this);
			if (g_frameDepth++)
			{
				const auto result = original(a_this);
				--g_frameDepth;
				return result;
			}

			const auto full = g_installMode == InstallMode::kFull;
			if (full)
			{
				g_activeFrame = {};
				g_phaseFrameActive = true;
			}
			const auto start = Counter();
			const auto result = original(a_this);
			const auto end = Counter();
			const auto elapsed = end - start;
			if (full)
				g_phaseFrameActive = false;
			--g_frameDepth;
			const auto sequence = ++g_sequence;
			if (full)
				FinishFrame(elapsed, sequence);
			const FrameTick tick{ sequence, end, elapsed, Milliseconds(elapsed) };
			DispatchFrameTicks(tick);
			return result;
		}

		template <Phase P, Hook H>
		static std::uintptr_t HKPhase(void* a_this) noexcept
		{
			const auto active = BeginPhase(P);
			const auto result = reinterpret_cast<TGeneric>(g_originals[static_cast<std::size_t>(H)])(a_this);
			EndPhase(P, active);
			return result;
		}

		static std::uintptr_t HKUpdateAudio(void* a_this, float a_delta) noexcept
		{
			const auto active = BeginPhase(Phase::kUpdateAudio);
			const auto result = reinterpret_cast<TFloat>(g_originals[static_cast<std::size_t>(Hook::kUpdateAudio)])(
				a_this, a_delta);
			EndPhase(Phase::kUpdateAudio, active);
			return result;
		}

		static std::uintptr_t HKUpdateImageSpace(void* a_this, bool a_updateAge) noexcept
		{
			const auto active = BeginPhase(Phase::kUpdateImageSpace);
			const auto result = reinterpret_cast<TBool>(g_originals[static_cast<std::size_t>(Hook::kUpdateImageSpace)])(
				a_this, a_updateAge);
			EndPhase(Phase::kUpdateImageSpace, active);
			return result;
		}

		static std::uintptr_t HKLoadQueuedPriority(void* a_this, std::uint32_t a_priority) noexcept
		{
			const auto active = BeginStall(
				g_loadQueuedActivity, g_activeFrame.loadQueuedPriority,
				g_activeFrame.loadQueuedDepth, g_activeFrame.loadQueuedStart);
			const auto result = reinterpret_cast<TPriority>(
				g_originals[static_cast<std::size_t>(Hook::kLoadQueuedPriority)])(a_this, a_priority);
			EndStall(
				g_activeFrame.loadQueuedPriority, g_activeFrame.loadQueuedDepth,
				g_activeFrame.loadQueuedStart, active);
			return result;
		}

		static std::uintptr_t HKClearLoadingTask(void* a_this) noexcept
		{
			const auto active = BeginStall(
				g_clearLoadingActivity, g_activeFrame.clearLoadingTask,
				g_activeFrame.clearLoadingDepth, g_activeFrame.clearLoadingStart);
			const auto result = reinterpret_cast<TGeneric>(
				g_originals[static_cast<std::size_t>(Hook::kClearLoadingTask)])(a_this);
			EndStall(
				g_activeFrame.clearLoadingTask, g_activeFrame.clearLoadingDepth,
				g_activeFrame.clearLoadingStart, active);
			return result;
		}

		static std::uintptr_t HKUpdateMessageBox(void* a_this) noexcept
		{
			const auto active = BeginPhase(Phase::kUpdateMessageBox);
			const auto result = g_messageOriginal(a_this);
			EndPhase(Phase::kUpdateMessageBox, active);
			return result;
		}

		[[nodiscard]] static std::array<std::uintptr_t, kHookCount> HookFunctions() noexcept
		{
			return {
				reinterpret_cast<std::uintptr_t>(&HKOnIdle),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kUpdateIOManager, Hook::kUpdateIOManager>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kGeneralUpdate, Hook::kGeneralUpdate>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kTreeUpdate, Hook::kTreeUpdate>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kAI, Hook::kAI>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kUpdateTimer, Hook::kUpdateTimer>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kPollControls, Hook::kPollControls>),
				reinterpret_cast<std::uintptr_t>(&HKUpdateAudio),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kUpdateCurrentGridCell, Hook::kUpdateCurrentGridCell>),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kUpdateSky, Hook::kUpdateSky>),
				reinterpret_cast<std::uintptr_t>(&HKUpdateImageSpace),
				reinterpret_cast<std::uintptr_t>(&HKPhase<Phase::kPostThreadsProcess, Hook::kPostThreadsProcess>),
				reinterpret_cast<std::uintptr_t>(&HKLoadQueuedPriority),
				reinterpret_cast<std::uintptr_t>(&HKClearLoadingTask)
			};
		}

		[[nodiscard]] static bool TryReadMemory(
			std::uintptr_t a_address, void* a_output, std::size_t a_size) noexcept
		{
			__try
			{
				std::memcpy(a_output, reinterpret_cast<const void*>(a_address), a_size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		[[nodiscard]] static HookActivitySnapshot::StallHookOwnership InspectStallHook(Hook a_hook) noexcept
		{
			const auto index = static_cast<std::size_t>(a_hook);
			HookActivitySnapshot::StallHookOwnership ownership;
			ownership.target = ResolveTargets()[index];
			ownership.targetReadable = TryReadMemory(
				ownership.target, ownership.targetBytes.data(), ownership.targetBytes.size());
			if (!ownership.targetReadable || ownership.targetBytes[0] != 0xE9)
				return ownership;
			std::int32_t displacement{};
			std::memcpy(&displacement, ownership.targetBytes.data() + 1, sizeof(displacement));
			const auto trampoline = ownership.target + 5 + static_cast<std::intptr_t>(displacement);
			static constexpr std::array<std::uint8_t, 6> stub{ 0xFF, 0x25, 0, 0, 0, 0 };
			std::array<std::uint8_t, stub.size()> actualStub{};
			if (!TryReadMemory(trampoline, actualStub.data(), actualStub.size()) ||
				actualStub != stub)
				return ownership;
			std::uintptr_t destination{};
			if (!TryReadMemory(
					trampoline + stub.size(), std::addressof(destination), sizeof(destination)))
				return ownership;
			ownership.owned = destination == HookFunctions()[index];
			return ownership;
		}

		[[nodiscard]] static std::uintptr_t BuildRelocatedGateway(
			std::uintptr_t a_target) noexcept
		{
			const auto length = RelocatedLength();
			auto& trampoline = REL::GetTrampoline();
			auto* gateway = static_cast<std::uint8_t*>(trampoline.allocate(length + 5));
			std::memcpy(gateway, reinterpret_cast<const void*>(a_target), length);

			const auto gatewayAddress = reinterpret_cast<std::uintptr_t>(gateway);
			constexpr auto relativeOffset = 5u;
			constexpr auto instructionEnd = 9u;
			const auto oldDisplacement = *reinterpret_cast<const std::int32_t*>(a_target + relativeOffset);
			const auto destination = a_target + instructionEnd + static_cast<std::intptr_t>(oldDisplacement);
			const auto newDisplacement = static_cast<std::int64_t>(destination) -
				static_cast<std::int64_t>(gatewayAddress + instructionEnd);
			const auto returnDisplacement = static_cast<std::int64_t>(a_target + length) -
				static_cast<std::int64_t>(gatewayAddress + length + 5);
			if (newDisplacement < INT32_MIN || newDisplacement > INT32_MAX ||
				returnDisplacement < INT32_MIN || returnDisplacement > INT32_MAX)
				return 0;
			*reinterpret_cast<std::int32_t*>(gateway + relativeOffset) =
				static_cast<std::int32_t>(newDisplacement);
			gateway[length] = 0xE9;
			*reinterpret_cast<std::int32_t*>(gateway + length + 1) =
				static_cast<std::int32_t>(returnDisplacement);
			FlushInstructionCache(GetCurrentProcess(), gateway, length + 5);
			return gatewayAddress;
		}

		[[nodiscard]] static bool InstallRelocatedHook(
			std::uintptr_t a_target, std::uintptr_t a_hook, std::size_t a_index) noexcept
		{
			g_originals[a_index] = BuildRelocatedGateway(a_target);
			if (!g_originals[a_index])
				return false;
			auto& trampoline = REL::GetTrampoline();
			trampoline.write_jmp<5>(a_target, a_hook);
			const auto length = RelocatedLength();
			if (length > 5)
				REL::WriteSafeFill(a_target + 5, REL::NOP, length - 5);
			return *reinterpret_cast<const std::uint8_t*>(a_target) == 0xE9;
		}

		[[nodiscard]] static bool RollBackPollCallSites() noexcept
		{
			const auto pollCalls = ResolvePollCallSites();
			while (g_pollCallsInstalled)
			{
				const auto index = g_pollCallsInstalled - 1;
				if (!REL::WriteSafe(
						pollCalls.sites[index],
						g_pollCallOriginals[index].data(),
						g_pollCallOriginals[index].size()) ||
					std::memcmp(
						reinterpret_cast<const void*>(pollCalls.sites[index]),
						g_pollCallOriginals[index].data(),
						g_pollCallOriginals[index].size()) != 0)
					return false;
				--g_pollCallsInstalled;
			}
			return true;
		}

		[[nodiscard]] static InstallResult InstallPollCallSites() noexcept
		{
			const auto pollCalls = ResolvePollCallSites();
			const auto expected = g_originals[static_cast<std::size_t>(Hook::kPollControls)];
			const auto hook = &HKPhase<Phase::kPollControls, Hook::kPollControls>;
			for (std::size_t index = 0; index < pollCalls.count; ++index)
			{
				const auto site = pollCalls.sites[index];
				auto& originalBytes = g_pollCallOriginals[index];
				std::memcpy(originalBytes.data(), reinterpret_cast<const void*>(site), originalBytes.size());
				const auto original = REL::GetTrampoline().write_call<5>(site, hook);
				if (original != expected ||
					*reinterpret_cast<const std::uint8_t*>(site) != 0xE8 ||
					std::memcmp(reinterpret_cast<const void*>(site), originalBytes.data(), originalBytes.size()) == 0)
				{
					const auto currentRestored =
						REL::WriteSafe(site, originalBytes.data(), originalBytes.size()) &&
						std::memcmp(reinterpret_cast<const void*>(site), originalBytes.data(), originalBytes.size()) == 0;
					const auto priorRestored = RollBackPollCallSites();
					if (!currentRestored || !priorRestored)
						REX::WARN("Frame Hitch profiler: PollControls call-site rollback verification failed."sv);
					return currentRestored && priorRestored ?
						InstallResult::kFailedClean : InstallResult::kFailedUnverified;
				}
				++g_pollCallsInstalled;
			}
			return InstallResult::kInstalled;
		}

		[[nodiscard]] static bool RollBackHooks(std::size_t a_count) noexcept
		{
			const auto targets = ResolveTargets();
			const auto& signatures = RuntimeSignatures();
			bool restored = true;
			while (a_count)
			{
				--a_count;
				const auto length = PatchedLength(a_count);
				if (!length)
				continue;
				const auto success =
				REL::WriteSafe(targets[a_count], signatures[a_count].data(), length) &&
				std::memcmp(
					reinterpret_cast<const void*>(targets[a_count]),
					signatures[a_count].data(), length) == 0;
				restored &= success;
				if (success)
				g_originals[a_count] = 0;
			}
			return restored;
		}

		[[nodiscard]] static InstallResult Install(InstallMode a_mode) noexcept
		{
			if (g_installed)
				return InstallResult::kInstalled;
			if (!ValidateTargets(a_mode))
				return InstallResult::kFailedClean;

			LARGE_INTEGER frequency{};
			if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
			{
				REX::WARN("Frame Hitch profiler: QueryPerformanceFrequency failed; installing nothing."sv);
				return InstallResult::kFailedClean;
			}
			g_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
			const auto threshold = std::clamp(nAdditionalFrameHitchThresholdMs.GetValue(), 1, 5000);
			g_thresholdTicks = g_frequency * static_cast<std::uint64_t>(threshold) / 1000;
			g_nextReportTicks = Counter() + g_frequency * kReportIntervalMs / 1000;

			const auto targets = ResolveTargets();
			const auto hooks = HookFunctions();
			if (a_mode == InstallMode::kFull && REL::GetTrampoline().free_size() < 256)
			{
				REX::WARN("Frame Hitch profiler: insufficient trampoline space; installing nothing."sv);
				return InstallResult::kFailedClean;
			}
			g_installMode = a_mode;
			const auto hookCount = a_mode == InstallMode::kFull ? kHookCount : 1;
			for (std::size_t index = 0; index < hookCount; ++index)
			{
				const auto installed =
					index == static_cast<std::size_t>(Hook::kPollControls) ?
					(g_originals[index] = targets[index]) != 0 :
					IsRelocatedHook(index) ?
						InstallRelocatedHook(targets[index], hooks[index], index) :
						(g_originals[index] = Detours::X64::DetourFunction(
							targets[index], hooks[index], Detours::X64Option::USE_REL32_JUMP)) != 0;
				if (!installed)
				{
					const auto restored = RollBackHooks(index + 1);
					REX::WARN("Frame Hitch profiler: function hook {} failed; rollback {}."sv,
						index, restored ? "completed"sv : "verification failed"sv);
					return restored ? InstallResult::kFailedClean : InstallResult::kFailedUnverified;
				}
			}
			if (a_mode == InstallMode::kTickOnly)
			{
				g_installed = true;
				REX::INFO("Frame Hitch profiler: frame-tick hook installed."sv);
				return InstallResult::kInstalled;
			}
			const auto loadQueuedOwnership = InspectStallHook(Hook::kLoadQueuedPriority);
			if (!loadQueuedOwnership.owned)
				LogOwnershipWarning("LoadQueuedPriority"sv, loadQueuedOwnership);
			const auto clearLoadingOwnership = InspectStallHook(Hook::kClearLoadingTask);
			if (!clearLoadingOwnership.owned)
				LogOwnershipWarning("ClearLoadingTask"sv, clearLoadingOwnership);

			const auto pollResult = InstallPollCallSites();
			if (pollResult != InstallResult::kInstalled)
			{
				const auto hooksRestored = RollBackHooks(kHookCount);
				const auto restored = pollResult == InstallResult::kFailedClean && hooksRestored;
				REX::WARN("Frame Hitch profiler: PollControls call-site hook failed; rollback {}."sv,
					restored ? "completed"sv : "verification failed"sv);
				return restored ? InstallResult::kFailedClean : InstallResult::kFailedUnverified;
			}

			const auto messageCall = targets.back();
			std::memcpy(g_messageCallOriginal.data(), reinterpret_cast<const void*>(messageCall), g_messageCallOriginal.size());
			const auto expected = REL::ID{ 813902, 2718226, 2718226 }.address();
			g_messageOriginal = reinterpret_cast<TGeneric>(expected);
			const auto original = REL::GetTrampoline().write_call<5>(messageCall, HKUpdateMessageBox);
			if (original != expected ||
				*reinterpret_cast<const std::uint8_t*>(messageCall) != 0xE8 ||
				std::memcmp(reinterpret_cast<const void*>(messageCall),
					g_messageCallOriginal.data(), g_messageCallOriginal.size()) == 0)
			{
				const auto messageRestored =
					REL::WriteSafe(messageCall, g_messageCallOriginal.data(), g_messageCallOriginal.size()) &&
					std::memcmp(reinterpret_cast<const void*>(messageCall),
						g_messageCallOriginal.data(), g_messageCallOriginal.size()) == 0;
				const auto pollsRestored = RollBackPollCallSites();
				const auto hooksRestored = RollBackHooks(kHookCount);
				REX::WARN("Frame Hitch profiler: message-box call hook failed; rollback {}."sv,
					messageRestored && pollsRestored && hooksRestored ? "completed"sv : "verification failed"sv);
				return messageRestored && pollsRestored && hooksRestored ?
					InstallResult::kFailedClean : InstallResult::kFailedUnverified;
			}
			g_installed = true;
			REX::INFO("Frame Hitch profiler: installed at {} ms; cell-load correlation uses a rolling hitch window because no authoritative cell-load state is exposed."sv,
				threshold);
			return InstallResult::kInstalled;
		}
	}

	bool ProfilerFrameHitch::RegisterFrameTick(FrameTickCallback a_callback) noexcept
	{
		return frameHitchProfilerDetail::RegisterFrameTick(a_callback);
	}

	bool ProfilerFrameHitch::HasFrameTickSubscribers() noexcept
	{
		return frameHitchProfilerDetail::HasFrameTickSubscribers();
	}

	void ProfilerFrameHitch::Install() noexcept
	{
		if (m_installed)
			return;
		auto* profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive())
		{
			REX::WARN("Frame Hitch profiler: profiler switches are disabled; installing nothing."sv);
			return;
		}
		if (ProfilerCore::IsFrameHitchEnabled())
		{
			const auto result = frameHitchProfilerDetail::Install(frameHitchProfilerDetail::InstallMode::kFull);
			m_installed = result == frameHitchProfilerDetail::InstallResult::kInstalled;
			if (!m_installed &&
				result == frameHitchProfilerDetail::InstallResult::kFailedClean &&
				HasFrameTickSubscribers())
			{
				const auto fallback = frameHitchProfilerDetail::Install(frameHitchProfilerDetail::InstallMode::kTickOnly);
				m_installed = fallback == frameHitchProfilerDetail::InstallResult::kInstalled;
				if (m_installed)
					REX::WARN("Frame Hitch profiler: full install failed cleanly; frame-tick-only fallback is active."sv);
				else
					REX::WARN("Frame Hitch profiler: frame-tick-only fallback failed."sv);
			}
		}
		else if (HasFrameTickSubscribers())
			m_installed =
				frameHitchProfilerDetail::Install(frameHitchProfilerDetail::InstallMode::kTickOnly) ==
				frameHitchProfilerDetail::InstallResult::kInstalled;
		else
			REX::WARN("Frame Hitch profiler: profiler switches are disabled; installing nothing."sv);
	}
}
