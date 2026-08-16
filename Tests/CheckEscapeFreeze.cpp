#include "../Addictol/Include/AdEscapeFreezeState.h"
#include "Harness.h"

#include <limits>

namespace vmm_tests
{
	void run_escape_freeze_checks(Runner& runner)
	{
		using namespace Addictol::EscapeFreeze;

		runner.test("escape freeze requires a primed stale renderer heartbeat", [] {
			WatchState state;
			require(!Observe(state, 0, 10, 0, 0, 1000).stallCandidateStarted, "initial sample created a candidate");
			require(!Observe(state, 100, 11, 0, 0, 1000).stallCandidateStarted, "heartbeat prime created a candidate");
			require(!Observe(state, 200, 11, 100, 1, 1000).stallCandidateStarted, "new lock created a candidate");
			require(!Observe(state, 1199, 11, 100, 1, 1000).stallCandidateStarted, "candidate fired before threshold");
			const auto detected = Observe(state, 1200, 11, 100, 1, 1000);
			require(detected.stallCandidateStarted, "sampled lock sequence and stale renderer created no candidate");
			require(detected.shouldCheckOwner, "candidate did not authorize owner evaluation");
		});

		runner.test("escape freeze classifies advancing frames as a healthy sampled sequence", [] {
			WatchState state;
			(void)Observe(state, 0, 1, 0, 0, 1000);
			(void)Observe(state, 100, 2, 55, 1, 1000);
			(void)Observe(state, 900, 3, 55, 1, 1000);
			const auto healthy = Observe(state, 1100, 4, 55, 1, 1000);
			require(healthy.healthySampleSequence, "advancing renderer was not classified as healthy");
			require(!healthy.stallCandidateStarted, "advancing renderer created a stall candidate");
			require(!Observe(state, 1200, 5, 55, 1, 1000).healthySampleSequence, "healthy sequence counted twice");
		});

		runner.test("escape freeze healthy sequence produces no log-worthy event", [] {
			WatchState state;
			(void)Observe(state, 0, 1, 0, 0, 1000);
			(void)Observe(state, 100, 2, 55, 1, 1000);
			(void)Observe(state, 900, 3, 55, 1, 1000);
			const auto healthy = Observe(state, 1100, 4, 55, 1, 1000);
			require(healthy.healthySampleSequence, "test sequence was not healthy");
			require(!healthy.corruptCountStarted, "healthy sequence produced a corruption event");
			require(!healthy.stallCandidateStarted, "healthy sequence produced a stall event");
			require(!healthy.shouldCheckOwner, "healthy sequence requested an owner check");
		});

		runner.test("escape freeze owner check dispatch is independent of logging", [] {
			Observation observation;
			observation.shouldCheckOwner = true;
			bool checked = false;
			DispatchOwnerCheck(observation, [&] {
				checked = true;
			});
			require(checked, "owner check request did not reach the orphan check");
		});

		runner.test("escape freeze stop path publishes after joining the worker", [] {
			int sequence = 0;
			bool joined = false;
			bool published = false;
			FinishWorker(
				[&] {
					joined = true;
					sequence = 1;
				},
				[&] {
					published = true;
					require(joined, "summary ran before worker join");
					require(sequence == 1, "summary did not immediately follow worker join");
					sequence = 2;
				});
			require(published, "stop path did not publish the summary");
			require(sequence == 2, "stop path did not complete summary publication");
		});

		runner.test("escape freeze restarts the hold window when ownership changes", [] {
			WatchState state;
			(void)Observe(state, 0, 1, 0, 0, 1000);
			(void)Observe(state, 100, 2, 0, 0, 1000);
			(void)Observe(state, 200, 2, 100, 1, 1000);
			const auto handoff = Observe(state, 1200, 2, 200, 1, 1000);
			require(!handoff.stallCandidateStarted, "owner handoff inherited the prior sample duration");
			require(!Observe(state, 2199, 2, 200, 1, 1000).stallCandidateStarted, "new owner fired early");
			require(Observe(state, 2200, 2, 200, 1, 1000).stallCandidateStarted, "new owner never reached threshold");
		});

		runner.test("escape freeze restarts the hold window after an unlocked sample", [] {
			WatchState state;
			(void)Observe(state, 0, 1, 0, 0, 100);
			(void)Observe(state, 10, 2, 0, 0, 100);
			(void)Observe(state, 20, 2, 100, 1, 100);
			(void)Observe(state, 80, 2, 0, 0, 100);
			(void)Observe(state, 120, 2, 100, 1, 100);
			require(!Observe(state, 219, 2, 100, 1, 100).stallCandidateStarted, "reacquired lock fired early");
			require(Observe(state, 220, 2, 100, 1, 100).stallCandidateStarted, "reacquired lock never reached threshold");
		});

		runner.test("escape freeze distinguishes renderer resumptions by intervention state", [] {
			WatchState state;
			(void)Observe(state, 0, 1, 0, 0, 100);
			(void)Observe(state, 10, 2, 0, 0, 100);
			(void)Observe(state, 20, 2, 100, 1, 100);
			require(Observe(state, 120, 2, 100, 1, 100).stallCandidateStarted, "unforced case created no candidate");
			require(!Observe(state, 125, 2, 0, 0, 100).rendererResumed, "zeroed lock self-confirmed resumption");
			const auto natural = Observe(state, 130, 3, 0, 0, 100);
			require(natural.rendererResumed && !natural.afterForcedRelease, "unforced resumption was mislabeled");

			(void)Observe(state, 140, 3, 0, 0, 100);
			(void)Observe(state, 150, 3, 200, 1, 100);
			require(Observe(state, 250, 3, 200, 1, 100).stallCandidateStarted, "forced case created no candidate");
			state.forcedRelease = true;
			(void)Observe(state, 255, 3, 200, 0, 100);
			(void)Observe(state, 260, 3, 300, 1, 100);
			const auto forced = Observe(state, 360, 4, 300, 1, 100);
			require(forced.rendererResumed && forced.afterForcedRelease, "post-force resumption was mislabeled");
			require(!forced.healthySampleSequence, "resumed candidate was also classified as healthy");
			require(!Observe(state, 370, 5, 300, 1, 100).healthySampleSequence, "candidate sequence was later reclassified");
		});

		runner.test("escape freeze reports each negative-count episode once", [] {
			WatchState state;
			require(Observe(state, 0, 1, 0, -1, 100).corruptCountStarted, "corruption was not reported");
			require(!Observe(state, 10, 1, 0, -1, 100).corruptCountStarted, "corruption repeated");
			(void)Observe(state, 20, 1, 0, 0, 100);
			require(Observe(state, 30, 1, 0, -1, 100).corruptCountStarted, "new corruption was hidden");
		});

		runner.test("escape freeze handles frame wrap and lock-pair layout", [] {
			WatchState state;
			(void)Observe(state, 0, std::numeric_limits<uint32_t>::max(), 0, 0, 100);
			(void)Observe(state, 10, 0, 0, 0, 100);
			require(state.frameHeartbeatReady, "frame wrap did not advance the heartbeat");

			constexpr uint64_t pair = 0x00000003F1234568ull;
			require(PairOwner(pair) == static_cast<int32_t>(0xF1234568u), "owner word decoded incorrectly");
			require(PairCount(pair) == 3, "count word decoded incorrectly");
		});
	}
}
