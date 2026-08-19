#include <Modules/AdModuleEscapeFreeze.h>
#include <Modules/AdEscapeFreezeState.h>
#include <Core/AdUtils.h>

#include <RE/B/BSGraphics.h>

#include <Windows.h>
#undef ERROR

#include <process.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesEscapeFreeze{ "Fixes"sv, "bEscapeFreeze"sv, true };
	static REX::TOML::I32<> nAdditionalSleepTimer{ "Additional"sv, "nSleepTimer"sv, 125 };
	static REX::TOML::I32<> nAdditionalMaxLockCount{ "Additional"sv, "nMaxLockCount"sv, 8 };

	namespace escapeFreezeDetail
	{
		constexpr int32_t kMinSleepMs = 1;
		constexpr int32_t kMaxSleepMs = 60'000;
		constexpr int32_t kMinLockPolls = 1;
		constexpr int32_t kMaxLockPolls = 1'000'000;

		struct Counters
		{
			std::atomic<uint64_t> sampledStallCandidates{};
			std::atomic<uint64_t> forcedOrphanReleases{};
			std::atomic<uint64_t> abortedOrphanReleases{};
			std::atomic<uint64_t> rendererResumptionsWithoutIntervention{};
			std::atomic<uint64_t> rendererResumptionsAfterForcedRelease{};
			std::atomic<uint64_t> healthySampleSequences{};
			std::atomic<uint64_t> corruptCountObservations{};
			std::atomic<uint64_t> ownerAliveObservations{};
			std::atomic<uint64_t> ownerUnknownObservations{};
			std::atomic<bool> stallCandidateReported{};
			std::atomic<bool> forcedOrphanReleaseReported{};
			std::atomic<bool> corruptCountReported{};
			std::atomic<bool> ownerAliveReported{};
			std::atomic<bool> ownerUnknownReported{};
		};

		struct RuntimeState
		{
			uint64_t* lockPair{};
			uint32_t* frameCount{};
			HANDLE wakeEvent{};
			HANDLE worker{};
			std::atomic<bool> stopping{};
			Counters counters;
			uint64_t qpcFrequency{};
			uint64_t thresholdTicks{};
			DWORD sleepMs{};
			uint32_t maxLockPolls{};
		};

		struct CounterSnapshot
		{
			uint64_t sampledStallCandidates{};
			uint64_t forcedOrphanReleases{};
			uint64_t abortedOrphanReleases{};
			uint64_t rendererResumptionsWithoutIntervention{};
			uint64_t rendererResumptionsAfterForcedRelease{};
			uint64_t healthySampleSequences{};
			uint64_t unresolvedCandidates{};
			uint64_t corruptCountObservations{};
			uint64_t ownerAliveObservations{};
			uint64_t ownerUnknownObservations{};
		};

		struct LockSnapshot
		{
			uint64_t pair{};
			LONG owner{};
			LONG count{};
		};

		enum class RangeAccess
		{
			kRead,
			kWrite
		};

		enum class OrphanReleaseResult
		{
			kReleased,
			kOwnerAlive,
			kOwnerUnknown,
			kStateChanged
		};

		struct OrphanReleaseAttempt
		{
			OrphanReleaseResult result;
			DWORD error{};
		};

		static REL::Relocation<int32_t*> g_conditionLockCount{
			REL::ID{ 998070, 2692050, 4799342 }
		};
		static std::atomic<RuntimeState*> g_runtime;
		static_assert(sizeof(LONG) == sizeof(int32_t));
		static_assert(sizeof(LONG64) == sizeof(uint64_t));
		static_assert(std::atomic_ref<uint32_t>::is_always_lock_free);
		static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);

		[[nodiscard]] static uint32_t ReadFrameCount(
			const RuntimeState& a_runtime) noexcept
		{
			return std::atomic_ref<uint32_t>(*a_runtime.frameCount).load(
				std::memory_order_relaxed);
		}

		[[nodiscard]] static LockSnapshot ReadLockSnapshot(
			const RuntimeState& a_runtime) noexcept
		{
			const auto pair = std::atomic_ref<uint64_t>(*a_runtime.lockPair).load(
				std::memory_order_relaxed);
			return {
				pair,
				static_cast<LONG>(EscapeFreeze::PairOwner(pair)),
				static_cast<LONG>(EscapeFreeze::PairCount(pair))
			};
		}

		[[nodiscard]] static uint64_t Counter() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<uint64_t>(value.QuadPart);
		}

		[[nodiscard]] static uint64_t Milliseconds(
			const RuntimeState& a_runtime,
			uint64_t a_ticks) noexcept
		{
			if (!a_runtime.qpcFrequency)
				return 0;

			const auto seconds = a_ticks / a_runtime.qpcFrequency;
			const auto remainder = a_ticks % a_runtime.qpcFrequency;
			if (seconds > std::numeric_limits<uint64_t>::max() / 1000)
				return std::numeric_limits<uint64_t>::max();
			return seconds * 1000 + remainder * 1000 / a_runtime.qpcFrequency;
		}

		[[nodiscard]] static bool IsAccessibleRange(
			const void* a_address,
			size_t a_size,
			RangeAccess a_access) noexcept
		{
			MEMORY_BASIC_INFORMATION memory{};
			if (!a_address || !a_size ||
				VirtualQuery(a_address, &memory, sizeof(memory)) != sizeof(memory) ||
				memory.State != MEM_COMMIT ||
				(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
				return false;

			const auto protection = memory.Protect & 0xFF;
			const auto writable =
				protection == PAGE_READWRITE ||
				protection == PAGE_WRITECOPY ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
			const auto readable =
				writable ||
				protection == PAGE_READONLY ||
				protection == PAGE_EXECUTE_READ;
			if (a_access == RangeAccess::kWrite ? !writable : !readable)
				return false;

			const auto start = reinterpret_cast<uintptr_t>(a_address);
			const auto end = start + a_size;
			const auto regionStart = reinterpret_cast<uintptr_t>(memory.BaseAddress);
			const auto regionEnd = regionStart + memory.RegionSize;
			return end >= start && regionEnd >= regionStart &&
				start >= regionStart && end <= regionEnd;
		}

		[[nodiscard]] static CounterSnapshot CaptureCounters(const RuntimeState& a_runtime) noexcept
		{
			CounterSnapshot snapshot;
			snapshot.sampledStallCandidates =
				a_runtime.counters.sampledStallCandidates.load(std::memory_order_relaxed);
			snapshot.forcedOrphanReleases =
				a_runtime.counters.forcedOrphanReleases.load(std::memory_order_relaxed);
			snapshot.abortedOrphanReleases =
				a_runtime.counters.abortedOrphanReleases.load(std::memory_order_relaxed);
			snapshot.rendererResumptionsWithoutIntervention =
				a_runtime.counters.rendererResumptionsWithoutIntervention.load(
					std::memory_order_relaxed);
			snapshot.rendererResumptionsAfterForcedRelease =
				a_runtime.counters.rendererResumptionsAfterForcedRelease.load(
					std::memory_order_relaxed);
			snapshot.healthySampleSequences =
				a_runtime.counters.healthySampleSequences.load(std::memory_order_relaxed);
			snapshot.corruptCountObservations =
				a_runtime.counters.corruptCountObservations.load(std::memory_order_relaxed);
			snapshot.ownerAliveObservations =
				a_runtime.counters.ownerAliveObservations.load(std::memory_order_relaxed);
			snapshot.ownerUnknownObservations =
				a_runtime.counters.ownerUnknownObservations.load(std::memory_order_relaxed);
			const auto resumptions =
				snapshot.rendererResumptionsWithoutIntervention +
				snapshot.rendererResumptionsAfterForcedRelease;
			snapshot.unresolvedCandidates =
				snapshot.sampledStallCandidates > resumptions ?
				snapshot.sampledStallCandidates - resumptions :
				0;
			return snapshot;
		}

		[[nodiscard]] static bool ReadTelemetry(
			EscapeFreezeMetricSource::Values& a_values) noexcept
		{
			const auto runtime = g_runtime.load(std::memory_order_acquire);
			if (!runtime)
				return false;
			const auto stats = CaptureCounters(*runtime);
			a_values = MetricDoubles(
				stats.sampledStallCandidates, stats.forcedOrphanReleases,
				stats.abortedOrphanReleases, stats.rendererResumptionsWithoutIntervention,
				stats.rendererResumptionsAfterForcedRelease, stats.healthySampleSequences,
				stats.corruptCountObservations, stats.ownerAliveObservations,
				stats.ownerUnknownObservations, stats.unresolvedCandidates);
			return true;
		}

		[[nodiscard]] static bool HasAnomaly(const CounterSnapshot& a_stats) noexcept
		{
			return a_stats.sampledStallCandidates ||
				a_stats.forcedOrphanReleases ||
				a_stats.abortedOrphanReleases ||
				a_stats.corruptCountObservations ||
				a_stats.ownerAliveObservations ||
				a_stats.ownerUnknownObservations;
		}

		static void LogShutdownSummary(const RuntimeState& a_runtime) noexcept
		{
			const auto stats = CaptureCounters(a_runtime);
			if (!HasAnomaly(stats))
				return;

			REX::INFO(
				"Escape Freeze shutdown summary: sampled stall candidates={}, forced orphan releases={}, "
				"aborted orphan releases={}, "
				"renderer resumptions without intervention={}, "
				"renderer resumptions after forced release={}, healthy sampled sequences={}, "
				"unresolved candidates={}, corrupt count observations={}, "
				"owner-alive observations={}, owner-unknown observations={}."sv,
				stats.sampledStallCandidates,
				stats.forcedOrphanReleases,
				stats.abortedOrphanReleases,
				stats.rendererResumptionsWithoutIntervention,
				stats.rendererResumptionsAfterForcedRelease,
				stats.healthySampleSequences,
				stats.unresolvedCandidates,
				stats.corruptCountObservations,
				stats.ownerAliveObservations,
				stats.ownerUnknownObservations);
		}

		[[nodiscard]] static OrphanReleaseAttempt TryReleaseOrphan(
			RuntimeState& a_runtime,
			const LockSnapshot& a_snapshot) noexcept
		{
			if (a_snapshot.owner == 0 || a_snapshot.count <= 0)
				return { OrphanReleaseResult::kOwnerUnknown, ERROR_INVALID_PARAMETER };

			auto ownerHandle = OpenThread(
				SYNCHRONIZE, FALSE, static_cast<DWORD>(a_snapshot.owner));
			if (ownerHandle)
			{
				const auto waitResult = WaitForSingleObject(ownerHandle, 0);
				const auto waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
				CloseHandle(ownerHandle);
				if (waitResult == WAIT_TIMEOUT)
					return { OrphanReleaseResult::kOwnerAlive };
				if (waitResult != WAIT_OBJECT_0)
					return { OrphanReleaseResult::kOwnerUnknown, waitError };
			}
			else
			{
				const auto openError = GetLastError();
				if (openError != ERROR_INVALID_PARAMETER)
					return { OrphanReleaseResult::kOwnerUnknown, openError };
			}

			const auto observed = static_cast<uint64_t>(InterlockedCompareExchange64(
				reinterpret_cast<volatile LONG64*>(a_runtime.lockPair),
				0,
				static_cast<LONG64>(a_snapshot.pair)));
			return observed == a_snapshot.pair ?
				OrphanReleaseAttempt{ OrphanReleaseResult::kReleased } :
				OrphanReleaseAttempt{ OrphanReleaseResult::kStateChanged };
		}

		static void ReportRendererResumption(
			RuntimeState& a_runtime,
			const EscapeFreeze::Observation& a_observation) noexcept
		{
			if (!a_observation.rendererResumed)
				return;

			if (a_observation.afterForcedRelease)
				a_runtime.counters.rendererResumptionsAfterForcedRelease.fetch_add(
					1, std::memory_order_relaxed);
			else
				a_runtime.counters.rendererResumptionsWithoutIntervention.fetch_add(
					1, std::memory_order_relaxed);
		}

		static void CheckOrphanedOwner(
			RuntimeState& a_runtime,
			EscapeFreeze::WatchState& a_state,
			uint64_t a_now,
			const LockSnapshot& a_snapshot) noexcept
		{
			if (a_state.forcedRelease ||
				(a_state.lastOwnerCheckQpc &&
					EscapeFreeze::Elapsed(a_now, a_state.lastOwnerCheckQpc) <
						a_runtime.thresholdTicks))
				return;

			a_state.lastOwnerCheckQpc = a_now;
			const auto attempt = TryReleaseOrphan(a_runtime, a_snapshot);
			switch (attempt.result)
			{
			case OrphanReleaseResult::kReleased:
			{
				a_runtime.counters.forcedOrphanReleases.fetch_add(1, std::memory_order_relaxed);
				a_state.forcedRelease = true;
				if (!a_runtime.counters.forcedOrphanReleaseReported.exchange(
						true, std::memory_order_relaxed))
				{
					const auto stats = CaptureCounters(a_runtime);
					REX::WARN(
						"Escape Freeze: force-released orphaned condition lock owned by terminated "
						"thread {} at recursion count {}; this intervention is not evidence that "
						"the renderer resumed. "
						"forced orphan releases={}, unresolved candidates={}."sv,
						static_cast<uint32_t>(a_snapshot.owner),
						a_snapshot.count,
						stats.forcedOrphanReleases,
						stats.unresolvedCandidates);
				}
				break;
			}
			case OrphanReleaseResult::kOwnerAlive:
				a_runtime.counters.ownerAliveObservations.fetch_add(1, std::memory_order_relaxed);
				if (!a_runtime.counters.ownerAliveReported.exchange(
						true, std::memory_order_relaxed))
				{
					REX::WARN(
						"Escape Freeze: sampled candidate owner thread {} is still alive; "
						"no forced release was attempted."sv,
						static_cast<uint32_t>(a_snapshot.owner));
				}
				break;
			case OrphanReleaseResult::kOwnerUnknown:
				a_runtime.counters.ownerUnknownObservations.fetch_add(1, std::memory_order_relaxed);
				if (!a_runtime.counters.ownerUnknownReported.exchange(
						true, std::memory_order_relaxed))
				{
					REX::WARN(
						"Escape Freeze: could not prove sampled candidate owner thread {} terminated "
						"(error {}); no forced release was attempted."sv,
						static_cast<uint32_t>(a_snapshot.owner),
						attempt.error);
				}
				break;
			case OrphanReleaseResult::kStateChanged:
				a_runtime.counters.abortedOrphanReleases.fetch_add(
					1, std::memory_order_relaxed);
				a_state.lastOwnerCheckQpc = 0;
				break;
			}
		}

		static void Poll(
			RuntimeState& a_runtime,
			EscapeFreeze::WatchState& a_state) noexcept
		{
			const auto now = Counter();
			const auto frameSequence = ReadFrameCount(a_runtime);
			const auto lock = ReadLockSnapshot(a_runtime);
			const auto observation = EscapeFreeze::Observe(
				a_state,
				now,
				frameSequence,
				static_cast<int32_t>(lock.owner),
				static_cast<int32_t>(lock.count),
				a_runtime.thresholdTicks);

			ReportRendererResumption(a_runtime, observation);

			if (observation.corruptCountStarted)
			{
				const auto corruptObservation =
					a_runtime.counters.corruptCountObservations.fetch_add(
						1, std::memory_order_relaxed) + 1;
				if (!a_runtime.counters.corruptCountReported.exchange(
						true, std::memory_order_relaxed))
				{
					REX::ERROR(
						"Escape Freeze: condition lock has corrupt negative recursion count {} "
						"with owner {}; no write was attempted. corrupt count observation {}."sv,
						lock.count,
						static_cast<uint32_t>(lock.owner),
						corruptObservation);
				}
			}

			if (observation.healthySampleSequence)
				a_runtime.counters.healthySampleSequences.fetch_add(1, std::memory_order_relaxed);

			if (observation.stallCandidateStarted)
			{
				a_runtime.counters.sampledStallCandidates.fetch_add(1, std::memory_order_relaxed);

				if (!a_runtime.counters.stallCandidateReported.exchange(
						true, std::memory_order_relaxed))
				{
					const auto stats = CaptureCounters(a_runtime);
					REX::WARN(
						"Escape Freeze: sampled stall candidate; condition lock was observed nonzero "
						"with owner {} at every poll for {} ms (latest recursion count {}), while "
						"renderer frame {} was unchanged across polls for {} ms. "
						"sampled candidates={}, unresolved candidates={}."sv,
						static_cast<uint32_t>(lock.owner),
						Milliseconds(a_runtime, observation.sampleSequenceTicks),
						lock.count,
						frameSequence,
						Milliseconds(a_runtime, observation.frameUnchangedTicks),
						stats.sampledStallCandidates,
						stats.unresolvedCandidates);
				}
			}

			EscapeFreeze::DispatchOwnerCheck(observation, [&]() noexcept {
				CheckOrphanedOwner(a_runtime, a_state, now, lock);
			});
		}

		unsigned __stdcall WorkerMain(void* a_context) noexcept
		{
			auto& runtime = *static_cast<RuntimeState*>(a_context);
			EscapeFreeze::WatchState state;

			while (!runtime.stopping.load(std::memory_order_acquire))
			{
				Poll(runtime, state);
				if (WaitForSingleObject(runtime.wakeEvent, runtime.sleepMs) == WAIT_OBJECT_0 &&
					runtime.stopping.load(std::memory_order_acquire))
					break;
			}

			return 0;
		}

		static void StopRuntime(RuntimeState* a_runtime) noexcept
		{
			if (!a_runtime)
				return;

			auto expected = a_runtime;
			g_runtime.compare_exchange_strong(
				expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);
			EscapeFreeze::FinishWorker(
				[&]() noexcept {
					if (a_runtime->worker)
					{
						a_runtime->stopping.store(true, std::memory_order_release);
						SetEvent(a_runtime->wakeEvent);
						WaitForSingleObject(a_runtime->worker, INFINITE);
						CloseHandle(a_runtime->worker);
						a_runtime->worker = nullptr;
					}
				},
				[&]() noexcept {
					LogShutdownSummary(*a_runtime);
				});
			if (a_runtime->wakeEvent)
			{
				CloseHandle(a_runtime->wakeEvent);
				a_runtime->wakeEvent = nullptr;
			}

			delete a_runtime;
		}

		static void DestroyRuntime() noexcept
		{
			StopRuntime(g_runtime.exchange(nullptr, std::memory_order_acq_rel));
		}
	}

	ModuleEscapeFreeze::ModuleEscapeFreeze() :
		Module("Escape Freeze", &bFixesEscapeFreeze),
		EscapeFreezeMetricSource(kEscapeFreezeMetricSchema, &escapeFreezeDetail::ReadTelemetry)
	{}

	bool ModuleEscapeFreeze::DoQuery() const noexcept
	{
		if (IsModDLLPresent("EscapeFreeze.dll"))
		{
			Skip("Standalone 'EscapeFreeze.dll' is installed, skipping module"sv);
			return false;
		}

		if (IsModDLLPresent("EscapeFreezeNG.dll"))
		{
			Skip("Standalone 'EscapeFreezeNG.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleEscapeFreeze::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace escapeFreezeDetail;

		if (g_runtime.load(std::memory_order_acquire))
		{
			REX::INFO("Escape Freeze: watcher is already installed."sv);
			return true;
		}

		const auto lockAddress = reinterpret_cast<uintptr_t>(g_conditionLockCount.get());
		const auto lockPairAddress = lockAddress - sizeof(LONG);
		if (lockAddress < sizeof(LONG) ||
			lockAddress % alignof(LONG) != 0 ||
			lockPairAddress % std::atomic_ref<uint64_t>::required_alignment != 0)
		{
			REX::ERROR(
				"Escape Freeze: condition-lock relocation {:X} is invalid or not atomically aligned."sv,
				lockAddress);
			return false;
		}

		auto lockPair = reinterpret_cast<uint64_t*>(lockPairAddress);
		if (!IsAccessibleRange(
				reinterpret_cast<const void*>(lockPairAddress),
				sizeof(uint64_t),
				RangeAccess::kWrite))
		{
			REX::ERROR(
				"Escape Freeze: condition-lock relocation {:X} is not a committed writable range."sv,
				lockAddress);
			return false;
		}

		auto graphicsState = RE::BSGraphics::State::GetSingleton();
		auto frameCount = graphicsState ? std::addressof(graphicsState->frameCount) : nullptr;
		const auto frameAddress = reinterpret_cast<uintptr_t>(frameCount);
		if (!frameAddress ||
			frameAddress % std::atomic_ref<uint32_t>::required_alignment != 0 ||
			!IsAccessibleRange(
				reinterpret_cast<const void*>(frameAddress),
				sizeof(uint32_t),
				RangeAccess::kRead))
		{
			REX::ERROR(
				"Escape Freeze: graphics frame-count address {:X} is invalid, unaligned, "
				"or not a committed readable range."sv,
				frameAddress);
			return false;
		}

		const auto configuredSleep = nAdditionalSleepTimer.GetValue();
		const auto configuredPolls = nAdditionalMaxLockCount.GetValue();
		const auto sleepMs = std::clamp(configuredSleep, kMinSleepMs, kMaxSleepMs);
		const auto maxLockPolls = std::clamp(configuredPolls, kMinLockPolls, kMaxLockPolls);
		if (sleepMs != configuredSleep)
			REX::WARN("Escape Freeze: nSleepTimer={} is out of range; using {}."sv,
				configuredSleep, sleepMs);
		if (maxLockPolls != configuredPolls)
			REX::WARN("Escape Freeze: nMaxLockCount={} is out of range; using {}."sv,
				configuredPolls, maxLockPolls);

		LARGE_INTEGER frequency{};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
		{
			REX::ERROR("Escape Freeze: QueryPerformanceFrequency failed."sv);
			return false;
		}

		auto runtime = new (std::nothrow) RuntimeState;
		if (!runtime)
		{
			REX::ERROR("Escape Freeze: runtime-state allocation failed."sv);
			return false;
		}

		runtime->lockPair = lockPair;
		runtime->frameCount = frameCount;
		runtime->qpcFrequency = static_cast<uint64_t>(frequency.QuadPart);
		runtime->sleepMs = static_cast<DWORD>(sleepMs);
		runtime->maxLockPolls = static_cast<uint32_t>(maxLockPolls);
		const auto thresholdMs =
			static_cast<uint64_t>(sleepMs) *
			static_cast<uint64_t>(maxLockPolls);
		runtime->thresholdTicks =
			runtime->qpcFrequency * (thresholdMs / 1000) +
			runtime->qpcFrequency * (thresholdMs % 1000) / 1000;
		runtime->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!runtime->wakeEvent)
		{
			REX::ERROR("Escape Freeze: worker event creation failed (error {})."sv, GetLastError());
			delete runtime;
			return false;
		}

		// Must survive F4SE's failure-path FreeLibrary.
		HMODULE module{};
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>(&g_runtime),
				&module))
		{
			REX::ERROR("Escape Freeze: could not pin Addictol.dll (error {})."sv, GetLastError());
			CloseHandle(runtime->wakeEvent);
			delete runtime;
			return false;
		}

		RuntimeState* expected = nullptr;
		if (!g_runtime.compare_exchange_strong(
				expected, runtime, std::memory_order_acq_rel, std::memory_order_acquire))
		{
			CloseHandle(runtime->wakeEvent);
			delete runtime;
			REX::INFO("Escape Freeze: watcher was installed concurrently."sv);
			return true;
		}

		if (std::atexit(DestroyRuntime) != 0)
		{
			REX::ERROR("Escape Freeze: shutdown registration failed."sv);
			StopRuntime(runtime);
			return false;
		}

		const auto workerValue = _beginthreadex(nullptr, 0, WorkerMain, runtime, 0, nullptr);
		runtime->worker = reinterpret_cast<HANDLE>(workerValue);
		if (!runtime->worker)
		{
			REX::ERROR("Escape Freeze: worker thread creation failed (error {})."sv, errno);
			StopRuntime(runtime);
			return false;
		}

		return true;
	}

}
