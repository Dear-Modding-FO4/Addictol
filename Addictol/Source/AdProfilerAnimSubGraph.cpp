#include <AdProfilerAnimSubGraph.h>
#include <AdAnimSubGraphRuntime.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>
#include <RE/D/DEFAULT_OBJECT.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Addictol
{
	static REX::TOML::Bool<> bProfilerAnimSubGraphSkipPreload{ "Profiler"sv, "bAnimSubGraphSkipPreload"sv, false };

	inline constexpr std::size_t kProjectionCapacity{ 32768 };
	inline constexpr std::size_t kFilenameScratchCapacity{ 4096 };

	namespace animSubGraphProfilerDetail
	{
		using namespace AnimSubGraphRuntime;
		using TGeneric = std::uintptr_t(*)(
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

		template <class F>
		class ScopeExit
		{
		public:
			explicit ScopeExit(F a_function) :
				function(std::move(a_function))
			{}

			ScopeExit(const ScopeExit&) = delete;
			ScopeExit& operator=(const ScopeExit&) = delete;

			~ScopeExit() { function(); }

		private:
			F function;
		};

		struct Metric
		{
			std::uint64_t ticks{};
			std::uint64_t calls{};
		};

		struct PassMetric
		{
			std::uint64_t ticks{};
			std::uint64_t maxTicks{};
			std::uint64_t calls{};
			std::uint64_t matchesAdded{};
		};

		struct RoleStats
		{
			Metric matched;
			Metric gather;
			Metric initialize;
			Metric load;
			Metric request;
			PassMetric movement;
			PassMetric activate1;
			PassMetric activate2;
			std::uint64_t rawFilenames{};
			std::uint64_t uniqueFilenames{};
			std::uint64_t filenameGathers{};
			std::uint64_t eligibleCalls{};
			std::uint64_t ineligibleCalls{};
			std::uint64_t projectedHits{};
			std::uint64_t projectedMisses{};
			std::uint64_t actualHits{};
			std::uint64_t actualMisses{};
		};

		struct BurstStats
		{
			std::array<RoleStats, 3> roles;
			std::array<std::uint64_t, 3> dropped{};
		};

		struct ProjectionState
		{
			std::uint64_t generation = UINT64_MAX;
			std::unordered_set<MatchKey, MatchKeyHash> keys;
			std::array<MatchKey, kMaxPendingMatches> pending;
			std::size_t pendingCount{};
		};

		static std::array<TGeneric, 4> g_originals;
		static std::array<std::array<std::uint8_t, 5>, 6> g_patchOriginals;
		static std::array<std::atomic<std::uint64_t>, 3> g_droppedSamples;
		static std::array<std::atomic<std::uint64_t>, 3> g_actualHits;
		static std::array<std::atomic<std::uint64_t>, 3> g_actualMisses;
		static std::mutex g_statsLock;
		static BurstStats g_stats;
		static std::uint64_t g_frequency;
		static bool g_installed;
		static bool g_timingHooksInstalled;
		static bool g_skipPreloadEnabled;
		static std::size_t g_patchedCallSites;
		static thread_local std::uint32_t g_currentRole = UINT32_MAX;
		static thread_local std::uint64_t g_initializeStart;
		static thread_local std::uint64_t g_loadTicksInInitialize;
		static thread_local bool g_insideInitialize;
		static thread_local RE::BSScrapArray<const RE::BGSKeyword*> g_keywordScratch;
		static thread_local bool g_keywordScratchReady;
		static thread_local ProjectionState g_projection;
		static thread_local std::array<std::uint32_t, 8> g_previousRoles;
		static thread_local std::array<std::uint64_t, 8> g_requestStarts;
		static thread_local std::size_t g_requestDepth;
		static thread_local std::uint32_t g_activateOrdinal;
		static thread_local std::size_t g_gatherDepth;
		static thread_local std::array<const char*, kFilenameScratchCapacity> g_filenameScratch;

		[[nodiscard]] static std::uint64_t Counter() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		[[nodiscard]] static double Milliseconds(std::uint64_t a_ticks) noexcept
		{
			return g_frequency ? static_cast<double>(a_ticks) * 1000.0 / static_cast<double>(g_frequency) : 0.0;
		}

		[[nodiscard]] static bool MakeKey(
			unsigned a_raceFormID, RE::IKeywordFormBase& a_actorKeywordForm, bool a_subIndex,
			MatchKey& a_key) noexcept
		{
			if (!g_keywordScratchReady)
				return false;
			g_keywordScratch.clear();
			a_actorKeywordForm.CollectAllKeywords(g_keywordScratch, nullptr);
			if (g_keywordScratch.size() > a_key.actorKeywords.size())
				return false;

			a_key = {};
			a_key.raceFormID = a_raceFormID;
			a_key.subIndex = a_subIndex;
			a_key.keywordCount = static_cast<std::uint16_t>(g_keywordScratch.size());
			for (std::uint32_t index = 0; index < g_keywordScratch.size(); ++index)
				a_key.actorKeywords[index] = reinterpret_cast<std::uintptr_t>(g_keywordScratch[index]);
			std::ranges::sort(a_key.actorKeywords.begin(), a_key.actorKeywords.begin() + a_key.keywordCount);
			const auto uniqueEnd = std::unique(a_key.actorKeywords.begin(), a_key.actorKeywords.begin() + a_key.keywordCount);
			a_key.keywordCount = static_cast<std::uint16_t>(uniqueEnd - a_key.actorKeywords.begin());
			return true;
		}

		static void DropSample(std::uint32_t a_role) noexcept
		{
			if (a_role < g_droppedSamples.size())
				g_droppedSamples[a_role].fetch_add(1, std::memory_order_relaxed);
		}

		static void AddMetric(Metric RoleStats::* a_metric, std::uint32_t a_role, std::uint64_t a_ticks) noexcept
		{
			if (a_role >= g_stats.roles.size())
				return;
			std::unique_lock lock{ g_statsLock, std::try_to_lock };
			if (!lock.owns_lock())
			{
				DropSample(a_role);
				return;
			}
			auto& metric = g_stats.roles[a_role].*a_metric;
			metric.ticks += a_ticks;
			++metric.calls;
		}

		static void AddPassMetric(
			PassMetric RoleStats::* a_metric, std::uint32_t a_role,
			std::uint64_t a_ticks, std::uint32_t a_matchesAdded) noexcept
		{
			if (a_role >= g_stats.roles.size())
				return;
			std::unique_lock lock{ g_statsLock, std::try_to_lock };
			if (!lock.owns_lock())
			{
				DropSample(a_role);
				return;
			}
			auto& metric = g_stats.roles[a_role].*a_metric;
			metric.ticks += a_ticks;
			metric.maxTicks = std::max(metric.maxTicks, a_ticks);
			metric.matchesAdded += a_matchesAdded;
			++metric.calls;
		}

		static void AddGatherMetric(
			std::uint32_t a_role, std::uint64_t a_ticks,
			std::uint32_t a_rawFilenames, std::uint32_t a_uniqueFilenames) noexcept
		{
			if (a_role >= g_stats.roles.size())
				return;
			std::unique_lock lock{ g_statsLock, std::try_to_lock };
			if (!lock.owns_lock())
			{
				DropSample(a_role);
				return;
			}
			auto& stats = g_stats.roles[a_role];
			stats.gather.ticks += a_ticks;
			++stats.gather.calls;
			stats.rawFilenames += a_rawFilenames;
			stats.uniqueFilenames += a_uniqueFilenames;
			++stats.filenameGathers;
		}

		static void LogBurst(BurstStats&& a_stats) noexcept
		{
			for (std::size_t role = 0; role < a_stats.roles.size(); ++role)
			{
				const auto& stats = a_stats.roles[role];
				if (!stats.request.calls && !stats.matched.calls && !stats.ineligibleCalls)
					continue;
				const auto projectedCalls = stats.projectedHits + stats.projectedMisses;
				const auto projectedRate = projectedCalls ?
					100.0 * static_cast<double>(stats.projectedHits) / static_cast<double>(projectedCalls) :
					0.0;
				const auto actualCalls = stats.actualHits + stats.actualMisses;
				const auto actualRate = actualCalls ?
					100.0 * static_cast<double>(stats.actualHits) / static_cast<double>(actualCalls) :
					0.0;
				REX::INFO("[Profiler/AnimSubGraph] role {}: request {:.3f} ms/{}; matched {:.3f} ms/{}; gather {:.3f} ms/{}; initialize {:.3f} ms/{}; load {:.3f} ms/{}; eligible {}; projected {}/{} hits ({:.1f}%); actual {}/{} hits ({:.1f}%); ineligible {}; dropped {}."sv,
					role,
					Milliseconds(stats.request.ticks), stats.request.calls,
					Milliseconds(stats.matched.ticks), stats.matched.calls,
					Milliseconds(stats.gather.ticks), stats.gather.calls,
					Milliseconds(stats.initialize.ticks), stats.initialize.calls,
					Milliseconds(stats.load.ticks), stats.load.calls,
					stats.eligibleCalls,
					stats.projectedHits, projectedCalls, projectedRate,
					stats.actualHits, actualCalls, actualRate,
					stats.ineligibleCalls, a_stats.dropped[role]);
				REX::INFO("[Profiler/AnimSubGraph] role {} passes: movement {:.3f} ms/{} max {:.3f} ms matches+{}; activate1 {:.3f} ms/{} max {:.3f} ms matches+{}; activate2 {:.3f} ms/{} max {:.3f} ms matches+{}; filenames raw {}/unique {} over {} gathers."sv,
					role,
					Milliseconds(stats.movement.ticks), stats.movement.calls,
					Milliseconds(stats.movement.maxTicks), stats.movement.matchesAdded,
					Milliseconds(stats.activate1.ticks), stats.activate1.calls,
					Milliseconds(stats.activate1.maxTicks), stats.activate1.matchesAdded,
					Milliseconds(stats.activate2.ticks), stats.activate2.calls,
					Milliseconds(stats.activate2.maxTicks), stats.activate2.matchesAdded,
					stats.rawFilenames, stats.uniqueFilenames, stats.filenameGathers);

				AnimSubGraphProfileEntry entry;
				entry.role = static_cast<std::uint32_t>(role);
				entry.request = { Milliseconds(stats.request.ticks), stats.request.calls };
				entry.matched = { Milliseconds(stats.matched.ticks), stats.matched.calls };
				entry.gather = { Milliseconds(stats.gather.ticks), stats.gather.calls };
				entry.initialize = { Milliseconds(stats.initialize.ticks), stats.initialize.calls };
				entry.load = { Milliseconds(stats.load.ticks), stats.load.calls };
				entry.eligibleCalls = stats.eligibleCalls;
				entry.projectedHits = stats.projectedHits;
				entry.projectedCalls = projectedCalls;
				entry.actualHits = stats.actualHits;
				entry.actualCalls = actualCalls;
				entry.ineligibleCalls = stats.ineligibleCalls;
				entry.droppedSamples = a_stats.dropped[role];
				entry.movement = {
					Milliseconds(stats.movement.ticks), Milliseconds(stats.movement.maxTicks),
					stats.movement.calls, stats.movement.matchesAdded
				};
				entry.activate1 = {
					Milliseconds(stats.activate1.ticks), Milliseconds(stats.activate1.maxTicks),
					stats.activate1.calls, stats.activate1.matchesAdded
				};
				entry.activate2 = {
					Milliseconds(stats.activate2.ticks), Milliseconds(stats.activate2.maxTicks),
					stats.activate2.calls, stats.activate2.matchesAdded
				};
				entry.rawFilenames = stats.rawFilenames;
				entry.uniqueFilenames = stats.uniqueFilenames;
				entry.filenameGathers = stats.filenameGathers;
				ProfilerCore::RecordAnimSubGraphRuntimeInterval(std::move(entry));
			}
		}

		static void CALLBACK BurstTimerCallback(
			[[maybe_unused]] PTP_CALLBACK_INSTANCE a_instance,
			[[maybe_unused]] void* a_context,
			[[maybe_unused]] PTP_TIMER a_timer) noexcept
		{
			BurstStats snapshot;
			{
				std::scoped_lock lock{ g_statsLock };
				snapshot = std::move(g_stats);
				g_stats = {};
			}
			for (std::size_t role = 0; role < snapshot.dropped.size(); ++role)
			{
				snapshot.dropped[role] = g_droppedSamples[role].exchange(0, std::memory_order_relaxed);
				snapshot.roles[role].actualHits = g_actualHits[role].exchange(0, std::memory_order_relaxed);
				snapshot.roles[role].actualMisses = g_actualMisses[role].exchange(0, std::memory_order_relaxed);
			}
			LogBurst(std::move(snapshot));
		}

		struct TimerOwner
		{
			PTP_TIMER timer{};
			~TimerOwner() { Close(); }

			[[nodiscard]] bool Create() noexcept
			{
				if (!timer)
					timer = CreateThreadpoolTimer(BurstTimerCallback, nullptr, nullptr);
				return timer != nullptr;
			}

			void Cancel() noexcept
			{
				if (timer)
					SetThreadpoolTimer(timer, nullptr, 0, 0);
			}

			void Schedule() noexcept
			{
				if (!timer)
					return;
				ULARGE_INTEGER due{};
				due.QuadPart = static_cast<ULONGLONG>(-2500000ll);
				FILETIME dueTime{ due.LowPart, due.HighPart };
				SetThreadpoolTimer(timer, &dueTime, 0, 0);
			}

			void Close() noexcept
			{
				if (!timer)
					return;
				SetThreadpoolTimer(timer, nullptr, 0, 0);
				WaitForThreadpoolTimerCallbacks(timer, TRUE);
				CloseThreadpoolTimer(timer);
				timer = nullptr;
			}
		};
		static TimerOwner g_burstTimer;

		static void CacheOutcome(Role a_role, bool a_hit) noexcept
		{
			const auto role = static_cast<std::size_t>(a_role);
			if (role >= g_actualHits.size())
				return;
			(a_hit ? g_actualHits[role] : g_actualMisses[role]).fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] static bool IsPending(const MatchKey& a_key) noexcept
		{
			for (std::size_t index = 0; index < g_projection.pendingCount; ++index)
			{
				if (g_projection.pending[index] == a_key)
					return true;
			}
			return false;
		}

		static bool GetMatchedSubGraphData(
			void* a_singleton, unsigned a_raceFormID, Role& a_role,
			RE::IKeywordFormBase& a_actorKeywordForm,
			RE::BSScrapArray<RE::IKeywordFormBase*>& a_targetKeywordForms,
			bool a_subIndex, RE::SubgraphIdentifier& a_outID, RE::BSFixedString& a_outRoot,
			PathArena& a_outPaths, TGetMatchedSubGraphData a_next) noexcept
		{
			const auto role = static_cast<std::uint32_t>(a_role);
			const auto oldCount = a_outPaths.size();
			const auto start = Counter();
			const auto result = a_next(
				a_singleton, a_raceFormID, a_role, a_actorKeywordForm, a_targetKeywordForms,
				a_subIndex, a_outID, a_outRoot, a_outPaths);
			const auto elapsed = Counter() - start;

			if (role >= g_stats.roles.size())
				return result;
			bool projectedHit = false;
			bool projectedMiss = false;
			const auto eligible = role == static_cast<std::uint32_t>(Role::kLocomotion) &&
				a_targetKeywordForms.empty() && result && IsProjectionReady();
			if (eligible)
			{
				MatchKey key;
				if (!MakeKey(a_raceFormID, a_actorKeywordForm, a_subIndex, key))
				{
					DropSample(role);
					return result;
				}
				projectedHit = g_projection.generation == GetProjectionGeneration() &&
					g_projection.keys.contains(key);
				projectedMiss = !projectedHit;
				const auto cacheable = a_outPaths.size() >= oldCount &&
					a_outPaths.size() - oldCount <= kMaxCachedPaths;
				if (projectedMiss && cacheable && !IsPending(key) &&
					g_projection.keys.size() + g_projection.pendingCount < GetProjectionCapacity() &&
					g_projection.pendingCount < g_projection.pending.size())
					g_projection.pending[g_projection.pendingCount++] = key;
			}

			std::unique_lock lock{ g_statsLock, std::try_to_lock };
			if (!lock.owns_lock())
			{
				DropSample(role);
				return result;
			}

			auto& stats = g_stats.roles[role];
			stats.matched.ticks += elapsed;
			++stats.matched.calls;
			if (eligible)
			{
				++stats.eligibleCalls;
				stats.projectedHits += projectedHit;
				stats.projectedMisses += projectedMiss;
			}
			else
				++stats.ineligibleCalls;
			return result;
		}

		static std::uintptr_t GatherPreloadAnimations(
			std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4,
			std::uintptr_t a_5, std::uintptr_t a_6, std::uintptr_t a_7, std::uintptr_t a_8,
			std::uintptr_t a_9, std::uintptr_t a_10, std::uintptr_t a_11, std::uintptr_t a_12) noexcept
		{
			auto* preloadFiles = reinterpret_cast<PathArena*>(a_6);
			if (!preloadFiles)
				return g_originals[0](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);

			const auto role = g_currentRole;
			const auto oldCount = preloadFiles->size();
			std::uintptr_t result = 1;
			std::uint64_t elapsed = 0;
			{
				const auto previousActivateOrdinal = g_activateOrdinal;
				ScopeExit restoreGatherState{ [previousActivateOrdinal]() noexcept {
					--g_gatherDepth;
					g_activateOrdinal = previousActivateOrdinal;
				} };
				g_activateOrdinal = 0;
				++g_gatherDepth;
				const auto start = Counter();
				result = g_skipPreloadEnabled ? 1 :
					g_originals[0](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
				elapsed = Counter() - start;
			}

			if (preloadFiles->size() < oldCount)
			{
				DropSample(role);
				return result;
			}

			const auto raw = preloadFiles->size() - oldCount;
			std::uint32_t unique = 0;
			std::size_t index = 0;
			for (const auto& filename : *preloadFiles)
			{
				if (index++ < oldCount)
					continue;
				const auto key = filename.c_str();
				bool found = false;
				for (std::uint32_t scratchIndex = 0; scratchIndex < unique; ++scratchIndex)
				{
					if (g_filenameScratch[scratchIndex] == key)
					{
						found = true;
						break;
					}
				}
				if (found)
					continue;
				if (unique >= g_filenameScratch.size())
				{
					DropSample(role);
					return result;
				}
				g_filenameScratch[unique++] = key;
			}
			AddGatherMetric(role, elapsed, raw, unique);
			return result;
		}

		[[nodiscard]] static std::uint32_t MatchCount(std::uintptr_t a_matches) noexcept
		{
			std::uint32_t count = 0;
			if (a_matches)
				std::memcpy(&count, reinterpret_cast<const void*>(a_matches + 0x18), sizeof(count));
			return count;
		}

		static std::uintptr_t CollectQualifyingIdlesForBehaviors(
			std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4,
			std::uintptr_t a_5, std::uintptr_t a_6, std::uintptr_t a_7, std::uintptr_t a_8,
			std::uintptr_t a_9, std::uintptr_t a_10, std::uintptr_t a_11, std::uintptr_t a_12) noexcept
		{
			PassMetric RoleStats::* metric = nullptr;
			if (a_3 == static_cast<std::uintptr_t>(RE::DEFAULT_OBJECT::kActionLargeMovementDelta))
				metric = &RoleStats::movement;
			else if (a_3 == static_cast<std::uintptr_t>(RE::DEFAULT_OBJECT::kActionActivate))
			{
				const auto ordinal = g_activateOrdinal++;
				if (ordinal == 0)
					metric = &RoleStats::activate1;
				else if (ordinal == 1)
					metric = &RoleStats::activate2;
			}

			const auto before = MatchCount(a_8);
			const auto start = Counter();
			const auto result = g_originals[3](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
			const auto elapsed = Counter() - start;
			const auto after = MatchCount(a_8);
			if (!metric || !g_gatherDepth || after < before)
				DropSample(g_currentRole);
			else
				AddPassMetric(metric, g_currentRole, elapsed, after - before);
			return result;
		}

		static std::uintptr_t InitializeSubGraph(
			std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4,
			std::uintptr_t a_5, std::uintptr_t a_6, std::uintptr_t a_7, std::uintptr_t a_8,
			std::uintptr_t a_9, std::uintptr_t a_10, std::uintptr_t a_11, std::uintptr_t a_12) noexcept
		{
			g_initializeStart = Counter();
			g_loadTicksInInitialize = 0;
			g_insideInitialize = true;
			const auto result = g_originals[1](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
			const auto elapsed = Counter() - g_initializeStart;
			g_insideInitialize = false;
			AddMetric(&RoleStats::initialize, g_currentRole, elapsed - std::min(elapsed, g_loadTicksInInitialize));
			return result;
		}

		static std::uintptr_t LoadSubGraph(
			std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4,
			std::uintptr_t a_5, std::uintptr_t a_6, std::uintptr_t a_7, std::uintptr_t a_8,
			std::uintptr_t a_9, std::uintptr_t a_10, std::uintptr_t a_11, std::uintptr_t a_12) noexcept
		{
			const auto start = Counter();
			const auto result = g_originals[2](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
			const auto elapsed = Counter() - start;
			if (g_insideInitialize)
				g_loadTicksInInitialize += elapsed;
			AddMetric(&RoleStats::load, g_currentRole, elapsed);
			return result;
		}

		static void RequestBegin(Role* a_role) noexcept
		{
			const auto depth = std::min(g_requestDepth, g_previousRoles.size() - 1);
			g_previousRoles[depth] = g_currentRole;
			g_requestStarts[depth] = Counter();
			g_currentRole = a_role ? static_cast<std::uint32_t>(*a_role) : UINT32_MAX;
			++g_requestDepth;
			g_burstTimer.Cancel();
			// Request entry runs before the graph-manager spinlock, so projection storage is prepared here.
			try
			{
				if (g_keywordScratch.capacity() < kKeywordScratchCapacity)
					g_keywordScratch.reserve(static_cast<std::uint32_t>(kKeywordScratchCapacity));
				g_keywordScratchReady = g_keywordScratch.capacity() >= kKeywordScratchCapacity;
				const auto generation = GetProjectionGeneration();
				if (g_projection.generation != generation)
				{
					ProjectionState fresh;
					fresh.generation = generation;
					g_projection = std::move(fresh);
				}
			}
			catch (...)
			{
				g_keywordScratchReady = false;
			}
		}

		static void RequestEnd([[maybe_unused]] Role* a_role) noexcept
		{
			if (!g_requestDepth)
				return;
			const auto depth = std::min(--g_requestDepth, g_previousRoles.size() - 1);
			AddMetric(&RoleStats::request, g_currentRole, Counter() - g_requestStarts[depth]);
			g_currentRole = g_previousRoles[depth];
			if (g_requestDepth)
				return;

			try
			{
				for (std::size_t index = 0; index < g_projection.pendingCount; ++index)
				{
					if (g_projection.keys.size() >= GetProjectionCapacity())
						break;
					g_projection.keys.emplace(g_projection.pending[index]);
				}
			}
			catch (...) {}
			g_projection.pendingCount = 0;
			g_burstTimer.Schedule();
		}

		[[nodiscard]] static std::array<std::uintptr_t, 5> FunctionTargets() noexcept
		{
			return {
				REL::Relocation<>{ REL::ID{ 1492580, 2193921, 2193921 } }.address(),
				REL::Relocation<>{ REL::ID{ 929639, 2236417, 2236417 } }.address(),
				REL::Relocation<>{ REL::ID{ 649876, 2257933, 2257933 } }.address(),
				REL::Relocation<>{ REL::ID{ 486161, 2258154, 2258154 } }.address(),
				REL::Relocation<>{ REL::ID{ 1353133, 2193988, 2193988 } }.address()
			};
		}

		[[nodiscard]] static std::array<std::span<const std::uint8_t>, 5> FunctionSignatures() noexcept
		{
			static constexpr std::uint8_t ogGather[]{
				0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55,
				0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB0, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8B,
				0xF9, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0x49, 0x63, 0xD9, 0x4D, 0x8B, 0xE0, 0x4C, 0x8B, 0xFA, 0x40
			};
			static constexpr std::uint8_t ogFunctor[]{
				0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x68, 0xD8, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00,
				0x00, 0x48, 0x83, 0xBA, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x0F,
				0x84, 0xB3, 0x02, 0x00, 0x00, 0x48, 0x89, 0x58, 0xE0, 0x48, 0x8D, 0x4D, 0x38, 0x4C, 0x89, 0x70
			};
			static constexpr std::uint8_t ogInitialize[]{
				0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xB2,
				0xA0, 0x03, 0x00, 0x00, 0x40, 0x32, 0xED, 0x45, 0x32, 0xED, 0x4D, 0x8B, 0xD1, 0x4D, 0x8B, 0xD8,
				0x48, 0x8B, 0xC2, 0x48, 0x85, 0xF6, 0x0F, 0x84, 0xAF, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x84, 0x24
			};
			static constexpr std::uint8_t ogLoad[]{
				0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
				0x55, 0x48, 0x83, 0xEC, 0x60, 0x44, 0x8B, 0x15, 0xB8, 0x3C, 0x02, 0x05, 0x65, 0x48, 0x8B, 0x04,
				0x25, 0x58, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xE9, 0x4A, 0x8B, 0x04, 0xD0, 0xB9, 0xC0, 0x09, 0x00
			};
			static constexpr std::uint8_t ogCollect[]{
				0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x44, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x55,
				0x53, 0x56, 0x48, 0x8D, 0x68, 0xC1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x8B, 0x59, 0x18,
				0x48, 0x89, 0x78, 0xE0, 0x4C, 0x89, 0x60, 0xD8, 0x4C, 0x89, 0x68, 0xD0, 0x33, 0xF6, 0x4C, 0x89
			};
			static constexpr std::uint8_t ngGather[]{
				0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41,
				0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xA0, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00, 0x48,
				0x8B, 0xF9, 0x49, 0x63, 0xD9, 0x48, 0x8D, 0x4D, 0xA0, 0x4D, 0x8B, 0xE0, 0x4C, 0x8B, 0xF2, 0x45
			};
			static constexpr std::uint8_t ngFunctor[]{
				0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x68, 0xD8, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00,
				0x00, 0x48, 0x83, 0xBA, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x0F,
				0x84, 0x08, 0x05, 0x00, 0x00, 0x48, 0x89, 0x58, 0xE0, 0x48, 0x8D, 0x4D, 0x38, 0x4C, 0x89, 0x70
			};
			static constexpr std::uint8_t ngInitialize[]{
				0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x4C, 0x8B,
				0xB2, 0xA0, 0x03, 0x00, 0x00, 0x45, 0x32, 0xED, 0x40, 0x32, 0xED, 0x4D, 0x8B, 0xD1, 0x4D, 0x8B,
				0xD8, 0x48, 0x8B, 0xC2, 0x4D, 0x85, 0xF6, 0x0F, 0x84, 0x75, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x84
			};
			static constexpr std::uint8_t ngLoad[]{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55,
				0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x44, 0x8B, 0x15, 0x1B, 0x16, 0xA3, 0x02, 0x48, 0x8B, 0xE9,
				0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD8, 0xB9, 0xC0, 0x09, 0x00
			};
			static constexpr std::uint8_t ngCollect[]{
				0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x44, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x55,
				0x53, 0x56, 0x48, 0x8D, 0x68, 0xC1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x8B, 0x59, 0x18,
				0x0F, 0x57, 0xC0, 0x48, 0x89, 0x78, 0xE0, 0x33, 0xF6, 0x4C, 0x89, 0x60, 0xD8, 0x4D, 0x8B, 0xE1
			};
			static constexpr std::uint8_t aeLoad[]{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55,
				0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x44, 0x8B, 0x15, 0xDB, 0x28, 0xAD, 0x02, 0x48, 0x8B, 0xE9,
				0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD8, 0xB9, 0xC0, 0x09, 0x00
			};
			if (RELEX::IsRuntimeOG())
				return { ogGather, ogFunctor, ogInitialize, ogLoad, ogCollect };
			if (RELEX::IsRuntimeNG())
				return { ngGather, ngFunctor, ngInitialize, ngLoad, ngCollect };
			return { ngGather, ngFunctor, ngInitialize, aeLoad, ngCollect };
		}

		[[nodiscard]] static bool ValidateFunctionTargets() noexcept
		{
			if (g_timingHooksInstalled)
				return true;
			const auto targets = FunctionTargets();
			const auto signatures = FunctionSignatures();
			for (std::size_t index = 0; index < targets.size(); ++index)
			{
				if (!ValidateUniqueSignature(targets[index], signatures[index]))
				{
					REX::WARN("AnimSubGraph profiler: function {} at {:X} failed unique signature validation; installing nothing."sv,
						index, targets[index]);
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] static std::array<std::uintptr_t, 6> TimingCallSites() noexcept
		{
			// Initialize covers 1 of 3 callers, so it is not comparable to whole-function runs.
			return {
				REL::Relocation<>{ REL::ID{ 929639, 2236417, 2236417 }, REL::Offset{ 0x124, 0x123, 0x123 } }.address(),
				REL::Relocation<>{ REL::ID{ 929639, 2236417, 2236417 }, REL::Offset{ 0x15F, 0x15F, 0x15F } }.address(),
				REL::Relocation<>{ REL::ID{ 649876, 2257933, 2257933 }, REL::Offset{ 0x60, 0x61, 0x61 } }.address(),
				REL::Relocation<>{ REL::ID{ 1492580, 2193921, 2193921 }, REL::Offset{ 0x207, 0x2EE, 0x2EE } }.address(),
				REL::Relocation<>{ REL::ID{ 1492580, 2193921, 2193921 }, REL::Offset{ 0x2B3, 0x39C, 0x39C } }.address(),
				REL::Relocation<>{ REL::ID{ 1492580, 2193921, 2193921 }, REL::Offset{ 0x304, 0x3ED, 0x3ED } }.address()
			};
		}

		[[nodiscard]] static std::array<std::uintptr_t, 6> TimingCallTargets() noexcept
		{
			const auto targets = FunctionTargets();
			return {
				targets[0], targets[2], targets[3],
				targets[4], targets[4], targets[4]
			};
		}

		[[nodiscard]] static bool ValidateTimingCallSites() noexcept
		{
			if (g_timingHooksInstalled)
				return true;
			const auto sites = TimingCallSites();
			const auto targets = TimingCallTargets();
			const auto count = sites.size();
			for (std::size_t index = 0; index < count; ++index)
			{
				const auto site = sites[index];
				if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8)
				{
					REX::WARN("AnimSubGraph profiler: timing call site {:X} is not E8; installing nothing."sv, site);
					return false;
				}
				const auto displacement = *reinterpret_cast<const std::int32_t*>(site + 1);
				const auto target = site + 5 + static_cast<std::intptr_t>(displacement);
				if (target != targets[index])
				{
					REX::WARN("AnimSubGraph profiler: timing call site {:X} targets {:X}, expected {:X}; installing nothing."sv,
						site, target, targets[index]);
					return false;
				}
			}
			return true;
		}

		static void RemoveTimingHooks() noexcept
		{
			const auto sites = TimingCallSites();
			while (g_patchedCallSites)
			{
				const auto index = --g_patchedCallSites;
				REL::WriteSafe(sites[index], g_patchOriginals[index].data(), g_patchOriginals[index].size());
			}
			g_originals = {};
			g_timingHooksInstalled = false;
		}

		[[nodiscard]] static bool InstallTimingHooks() noexcept
		{
			const auto sites = TimingCallSites();
			const auto targets = TimingCallTargets();
			const std::array<TGeneric, 6> hooks{
				&GatherPreloadAnimations, &InitializeSubGraph, &LoadSubGraph,
				&CollectQualifyingIdlesForBehaviors, &CollectQualifyingIdlesForBehaviors,
				&CollectQualifyingIdlesForBehaviors
			};
			const auto count = sites.size();
			for (std::size_t index = 0; index < count; ++index)
				std::memcpy(g_patchOriginals[index].data(), reinterpret_cast<const void*>(sites[index]), g_patchOriginals[index].size());

			g_originals[0] = reinterpret_cast<TGeneric>(targets[0]);
			g_originals[1] = reinterpret_cast<TGeneric>(targets[1]);
			g_originals[2] = reinterpret_cast<TGeneric>(targets[2]);
			g_originals[3] = reinterpret_cast<TGeneric>(targets[3]);

			auto& trampoline = REL::GetTrampoline();
			for (std::size_t index = 0; index < count; ++index)
			{
				const auto original = trampoline.write_call<5>(sites[index], hooks[index]);
				g_patchedCallSites = index + 1;
				if (original != targets[index])
				{
					RemoveTimingHooks();
					REX::WARN("AnimSubGraph profiler: timing-hook installation failed; changes rolled back."sv);
					return false;
				}
			}
			g_timingHooksInstalled = true;
			return true;
		}

		[[nodiscard]] static bool Validate() noexcept
		{
			if (g_installed)
				return true;
			// Skip-preload changes behavior, so it reads its own gate rather than the profiler option.
			g_skipPreloadEnabled = bProfilerAnimSubGraphSkipPreload.GetValue();
			if (!ValidateFunctionTargets() || !ValidateTimingCallSites())
				return false;
			return ValidateCallSites() && ValidateRequestHook();
		}

		[[nodiscard]] static bool Install() noexcept
		{
			if (g_installed)
				return true;
			if (!Validate())
				return false;

			LARGE_INTEGER frequency{};
			if (!QueryPerformanceFrequency(&frequency))
			{
				REX::WARN("AnimSubGraph profiler: QueryPerformanceFrequency failed."sv);
				return false;
			}
			g_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
			if (!g_burstTimer.Create())
			{
				REX::WARN("AnimSubGraph profiler: failed to create the burst timer."sv);
				return false;
			}
			if (!InstallTimingHooks())
			{
				g_burstTimer.Close();
				return false;
			}

			SetProjectionState(GetProjectionGeneration(), IsProjectionReady(), kProjectionCapacity);
			SetProfilerRequestCallbacks(RequestBegin, RequestEnd);
			SetCacheOutcomeCallback(CacheOutcome);
			if (!InstallRequestHook())
			{
				SetProfilerRequestCallbacks(nullptr, nullptr);
				SetCacheOutcomeCallback(nullptr);
				RemoveTimingHooks();
				g_burstTimer.Close();
				return false;
			}
			SetProfilerHandler(GetMatchedSubGraphData);
			if (!AnimSubGraphRuntime::Install())
			{
				SetProfilerHandler(nullptr);
				SetProfilerRequestCallbacks(nullptr, nullptr);
				SetCacheOutcomeCallback(nullptr);
				ReleaseRequestHook();
				RemoveTimingHooks();
				g_burstTimer.Close();
				return false;
			}

			g_installed = true;
			REX::INFO("[Profiler/AnimSubGraph] Timing hooks installed."sv);
			return true;
		}
	}

	void ProfilerAnimSubGraph::Install() noexcept
	{
		if (m_installed)
			return;
		auto* profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive() || !ProfilerCore::IsAnimSubGraphEnabled())
		{
			REX::WARN("AnimSubGraph profiler: profiler switches are disabled; installing nothing."sv);
			return;
		}
		m_installed = animSubGraphProfilerDetail::Install();
	}

	void ProfilerAnimSubGraph::HandleMessage(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (ProfilerCore::IsAnimSubGraphEnabled() && a_msg &&
			(a_msg->type == F4SE::MessagingInterface::kPreLoadGame ||
			a_msg->type == F4SE::MessagingInterface::kNewGame) &&
			!AnimSubGraphRuntime::IsCacheActive())
			AnimSubGraphRuntime::SetProjectionState(
				AnimSubGraphRuntime::GetProjectionGeneration() + 2, true,
				AnimSubGraphRuntime::GetProjectionCapacity());
	}
}
