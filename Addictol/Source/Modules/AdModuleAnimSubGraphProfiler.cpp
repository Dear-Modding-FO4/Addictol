#include <Modules/AdModuleAnimSubGraphProfiler.h>
#include <AdAnimSubGraphRuntime.h>
#include <AdUtils.h>
#include <detours/Detours.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Addictol
{
	static REX::TOML::Bool<> bProfilerAnimSubGraphProfiler{ "Profiler"sv, "bAnimSubGraphProfiler"sv, false };

	inline constexpr std::size_t kProjectionCapacity{ 32768 };

	namespace animSubGraphProfilerDetail
	{
		using namespace AnimSubGraphRuntime;
		using TGeneric = std::uintptr_t(*)(
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
			std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

		struct Metric
		{
			std::uint64_t ticks{};
			std::uint64_t calls{};
		};

		struct RoleStats
		{
			Metric matched;
			Metric gather;
			Metric initialize;
			Metric load;
			Metric request;
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

		static std::array<TGeneric, 3> g_originals;
		static std::array<std::atomic<std::uint64_t>, 3> g_droppedSamples;
		static std::array<std::atomic<std::uint64_t>, 3> g_actualHits;
		static std::array<std::atomic<std::uint64_t>, 3> g_actualMisses;
		static std::mutex g_statsLock;
		static BurstStats g_stats;
		static std::uint64_t g_frequency;
		static bool g_installed;
		static bool g_functionHooksInstalled;
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
			const auto start = Counter();
			const auto result = g_originals[0](a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
			AddMetric(&RoleStats::gather, g_currentRole, Counter() - start);
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

		[[nodiscard]] static std::array<std::uintptr_t, 3> FunctionTargets() noexcept
		{
			return {
				REL::Relocation<>{ REL::ID{ 1492580, 2193921, 2193921 } }.address(),
				REL::Relocation<>{ REL::ID{ 649876, 2257933, 2257933 } }.address(),
				REL::Relocation<>{ REL::ID{ 486161, 2258154, 2258154 } }.address()
			};
		}

		[[nodiscard]] static std::array<std::span<const std::uint8_t>, 3> FunctionSignatures() noexcept
		{
			// 16 bytes was not unique for this one on OG (6 hits); the uniqueness scan needs 24+.
			static constexpr std::uint8_t og0[]{
				0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55,
				0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB0, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8B,
				0xF9, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0x49, 0x63, 0xD9, 0x4D, 0x8B, 0xE0, 0x4C, 0x8B, 0xFA, 0x40
			};
			static constexpr std::uint8_t og1[]{
				0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xB2
			};
			static constexpr std::uint8_t og2[]{
				0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41
			};
			static constexpr std::uint8_t ng0[]{
				0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41
			};
			static constexpr std::uint8_t ng1[]{
				0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x4C, 0x8B
			};
			static constexpr std::uint8_t ng2[]{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55
			};
			if (RELEX::IsRuntimeOG())
				return { og0, og1, og2 };
			return { ng0, ng1, ng2 };
		}

		[[nodiscard]] static bool ValidateFunctionTargets() noexcept
		{
			if (g_functionHooksInstalled)
				return true;
			const auto targets = FunctionTargets();
			const auto signatures = FunctionSignatures();
			for (std::size_t index = 0; index < targets.size(); ++index)
			{
				if (!ValidateUniqueSignature(targets[index], signatures[index]))
				{
					REX::WARN("AnimSubGraph profiler: function {} at {:X} failed unique signature validation."sv, index, targets[index]);
					return false;
				}
			}
			return true;
		}

		static void RemoveFunctionHooks() noexcept
		{
			for (std::size_t index = g_originals.size(); index > 0; --index)
			{
				auto& original = g_originals[index - 1];
				if (original)
				{
					Detours::X64::DetourRemove(reinterpret_cast<std::uintptr_t>(original));
					original = nullptr;
				}
			}
			g_functionHooksInstalled = false;
		}

		[[nodiscard]] static bool InstallFunctionHooks() noexcept
		{
			const auto targets = FunctionTargets();
			const std::array<TGeneric, 3> hooks{
				&GatherPreloadAnimations, &InitializeSubGraph, &LoadSubGraph
			};
			for (std::size_t index = 0; index < targets.size(); ++index)
			{
				g_originals[index] = reinterpret_cast<TGeneric>(
					RELEX::DetourJump(targets[index], reinterpret_cast<std::uintptr_t>(hooks[index])));
				if (!g_originals[index])
				{
					RemoveFunctionHooks();
					REX::WARN("AnimSubGraph profiler: timing-hook installation failed; changes rolled back."sv);
					return false;
				}
			}
			g_functionHooksInstalled = true;
			return true;
		}

		[[nodiscard]] static bool Validate() noexcept
		{
			return g_installed || (ValidateCallSites() && ValidateRequestHook() && ValidateFunctionTargets());
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
			if (!InstallFunctionHooks())
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
				RemoveFunctionHooks();
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
				RemoveFunctionHooks();
				g_burstTimer.Close();
				return false;
			}

			g_installed = true;
			REX::INFO("[Profiler/AnimSubGraph] Timing hooks installed."sv);
			return true;
		}
	}

	ModuleAnimSubGraphProfiler::ModuleAnimSubGraphProfiler() :
		Module("Animation Subgraph Profiler", &bProfilerAnimSubGraphProfiler, {
			F4SE::MessagingInterface::kPreLoadGame,
			F4SE::MessagingInterface::kNewGame })
	{}

	bool ModuleAnimSubGraphProfiler::DoQuery() const noexcept
	{
		return animSubGraphProfilerDetail::Validate();
	}

	bool ModuleAnimSubGraphProfiler::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return animSubGraphProfilerDetail::Install();
	}

	bool ModuleAnimSubGraphProfiler::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kPreLoadGame ||
			a_msg->type == F4SE::MessagingInterface::kNewGame) &&
			!AnimSubGraphRuntime::IsCacheActive())
			AnimSubGraphRuntime::SetProjectionState(
				AnimSubGraphRuntime::GetProjectionGeneration() + 2, true,
				AnimSubGraphRuntime::GetProjectionCapacity());
		return true;
	}

	bool ModuleAnimSubGraphProfiler::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
