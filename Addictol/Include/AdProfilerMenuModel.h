#pragma once

#include <AdProfilerAllocator.h>
#include <AdProfilerBA2.h>
#include <AdProfilerCore.h>
#include <AdProfilerMenu.h>
#include <AdTextureOneShot.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Addictol
{
	struct ProfilerMenuPanelState
	{
		uint64_t refreshedAtQpc{ 0 };
		uint64_t refreshTicks{ 0 };
		bool hasData{ false };
	};

	struct ProfilerMenuStatus
	{
		std::string sessionID;
		uint64_t saveLoadEpoch{ 0 };
		uint64_t monotonicUs{ 0 };
		bool profilerActive{ false };
		bool frameHitchEnabled{ false };
		bool frameHitchInstalled{ false };
		bool allocatorEnabled{ false };
		bool allocatorInstalled{ false };
		bool ba2TimingEnabled{ false };
		bool ba2Recording{ false };
		bool memoryTrackingEnabled{ false };
		bool moduleProfilingEnabled{ false };
		bool csvExportEnabled{ false };
	};

	struct ProfilerMenuOverviewCache
	{
		ProfilerMenuPanelState state;
		ProfilerMenuStatus status;
		FrameHitchProfileEntry frame;
		bool hasFrame{ false };
		MemorySnapshot memory;
		bool hasMemory{ false };
		BA2PublishedSnapshot decompression;
		bool hasDecompression{ false };
		AllocatorProfileEntry allocator;
		bool hasAllocator{ false };
		TextureOneShot::CountersSnapshot texture;
		LogControl::Level logLevel{ LogControl::Level::kInfo };
		LogControl::Level logFlushLevel{ LogControl::Level::kInfo };
		LogControl::Stats logStats;
		size_t moduleCount{ 0 };
		size_t moduleFailures{ 0 };
		size_t moduleSkips{ 0 };
	};

	struct ProfilerMenuFrameHitchCache
	{
		ProfilerMenuPanelState state;
		FrameHitchProfileEntry latest;
		bool hasLatest{ false };
		std::vector<FrameHitchIntervalSummary> intervals;
		std::array<float, kFrameHitchProfileEntryCapacity> mean{};
		std::array<float, kFrameHitchProfileEntryCapacity> p95{};
		std::array<float, kFrameHitchProfileEntryCapacity> p99{};
		std::array<float, kFrameHitchProfileEntryCapacity> max{};
		size_t intervalCount{ 0 };
	};

	struct ProfilerMenuDecompressionCache
	{
		ProfilerMenuPanelState state;
		BA2PublishedSnapshot published;
		bool recording{ false };
	};

	struct ProfilerMenuAllocatorCache
	{
		ProfilerMenuPanelState state;
		AllocatorProfileEntry latest;
		bool hasLatest{ false };
		std::vector<AllocatorProfileEntry> intervals;
	};

	struct ProfilerMenuMemoryCache
	{
		ProfilerMenuPanelState state;
		std::vector<MemorySnapshot> snapshots;
	};

	struct ProfilerMenuModulesCache
	{
		ProfilerMenuPanelState state;
		std::vector<ModuleProfileEntry> entries;
		std::vector<size_t> order;
		std::array<char, 64> filter{};
		int sortColumn{ 0 };
		bool sortAscending{ true };
	};

	struct ProfilerMenuTextureCache
	{
		ProfilerMenuPanelState state;
		TextureOneShot::CountersSnapshot counters;
	};

	// UI-owned copies of profiler data; every refresh releases its lock before any drawing.
	class ProfilerMenuModel
	{
	public:
		ProfilerMenuModel() = default;

		ProfilerMenuModel(const ProfilerMenuModel&) = delete;
		ProfilerMenuModel& operator=(const ProfilerMenuModel&) = delete;

		// Inactive panels return before timing or reading any profiler state.
		void RefreshPanel(
			ProfilerMenuTab a_tab,
			bool a_active,
			uint64_t a_nowQpc,
			uint64_t a_qpcFrequency,
			uint32_t a_refreshMs) noexcept;

		void Reserve() noexcept;

		[[nodiscard]] const ProfilerMenuOverviewCache& Overview() const noexcept { return m_overview; }
		[[nodiscard]] const ProfilerMenuFrameHitchCache& FrameHitch() const noexcept { return m_frameHitch; }
		[[nodiscard]] const ProfilerMenuDecompressionCache& Decompression() const noexcept { return m_decompression; }
		[[nodiscard]] const ProfilerMenuAllocatorCache& Allocator() const noexcept { return m_allocator; }
		[[nodiscard]] const ProfilerMenuMemoryCache& Memory() const noexcept { return m_memory; }
		[[nodiscard]] const ProfilerMenuModulesCache& Modules() const noexcept { return m_modules; }
		[[nodiscard]] const ProfilerMenuTextureCache& Texture() const noexcept { return m_texture; }

		// Panels own their filter and sort state, which lives with the cache it drives.
		[[nodiscard]] ProfilerMenuOverviewCache& MutableOverview() noexcept { return m_overview; }
		[[nodiscard]] ProfilerMenuModulesCache& MutableModules() noexcept { return m_modules; }

	private:
		void RefreshOverview() noexcept;
		void RefreshFrameHitch() noexcept;
		void RefreshDecompression() noexcept;
		void RefreshAllocator() noexcept;
		void RefreshMemory() noexcept;
		void RefreshModules() noexcept;
		void RefreshTexture() noexcept;

		ProfilerMenuOverviewCache m_overview;
		ProfilerMenuFrameHitchCache m_frameHitch;
		ProfilerMenuDecompressionCache m_decompression;
		ProfilerMenuAllocatorCache m_allocator;
		ProfilerMenuMemoryCache m_memory;
		ProfilerMenuModulesCache m_modules;
		ProfilerMenuTextureCache m_texture;
		std::vector<MemorySnapshot> m_overviewMemoryScratch;
		std::vector<ModuleProfileEntry> m_overviewModuleScratch;
	};
}
