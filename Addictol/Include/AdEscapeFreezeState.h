#pragma once

#include <cstdint>

namespace Addictol::EscapeFreeze
{
	struct WatchState
	{
		bool sampleSequenceActive{};
		bool sampleSequenceClassified{};
		bool corruptReported{};
		bool sampledStallActive{};
		bool forcedRelease{};
		bool ownerResultReported{};
		bool frameSampled{};
		bool frameHeartbeatReady{};
		std::int32_t lastOwner{};
		std::uint32_t lastFrame{};
		std::uint64_t sampleSequenceStartQpc{};
		std::uint64_t sampleSequenceStartFrame{};
		std::uint64_t candidateQpc{};
		std::uint64_t candidateFrame{};
		std::uint64_t lastFrameAdvanceQpc{};
		std::uint64_t lastOwnerCheckQpc{};
	};

	struct Observation
	{
		bool corruptCountStarted{};
		bool healthySampleSequence{};
		bool stallCandidateStarted{};
		bool rendererResumed{};
		bool afterForcedRelease{};
		bool shouldCheckOwner{};
		std::uint64_t sampleSequenceTicks{};
		std::uint64_t frameUnchangedTicks{};
	};

	[[nodiscard]] constexpr std::uint64_t Elapsed(
		std::uint64_t a_now,
		std::uint64_t a_start) noexcept
	{
		return a_now >= a_start ? a_now - a_start : 0;
	}

	[[nodiscard]] constexpr std::int32_t PairOwner(std::uint64_t a_pair) noexcept
	{
		return static_cast<std::int32_t>(static_cast<std::uint32_t>(a_pair));
	}

	[[nodiscard]] constexpr std::int32_t PairCount(std::uint64_t a_pair) noexcept
	{
		return static_cast<std::int32_t>(static_cast<std::uint32_t>(a_pair >> 32));
	}

	[[nodiscard]] constexpr Observation Observe(
		WatchState& a_state,
		std::uint64_t a_now,
		std::uint32_t a_frame,
		std::int32_t a_owner,
		std::int32_t a_count,
		std::uint64_t a_threshold) noexcept
	{
		Observation result;
		bool frameAdvanced = false;
		if (!a_state.frameSampled)
		{
			a_state.frameSampled = true;
			a_state.lastFrame = a_frame;
			a_state.lastFrameAdvanceQpc = a_now;
		}
		else if (a_frame != a_state.lastFrame)
		{
			a_state.lastFrame = a_frame;
			a_state.lastFrameAdvanceQpc = a_now;
			a_state.frameHeartbeatReady = true;
			frameAdvanced = true;
		}

		if (a_state.sampledStallActive && frameAdvanced)
		{
			result.rendererResumed = true;
			result.afterForcedRelease = a_state.forcedRelease;
			a_state.sampledStallActive = false;
			a_state.forcedRelease = false;
			a_state.ownerResultReported = false;
		}

		if (a_count < 0)
		{
			result.corruptCountStarted = !a_state.corruptReported;
			a_state.corruptReported = true;
			a_state.sampleSequenceActive = false;
			a_state.sampleSequenceClassified = a_state.sampledStallActive;
			return result;
		}

		a_state.corruptReported = false;
		if (a_count == 0)
		{
			a_state.sampleSequenceActive = false;
			a_state.sampleSequenceClassified = a_state.sampledStallActive;
			return result;
		}

		if (!a_state.sampleSequenceActive || a_owner != a_state.lastOwner)
		{
			a_state.sampleSequenceActive = true;
			a_state.lastOwner = a_owner;
			a_state.sampleSequenceStartQpc = a_now;
			a_state.sampleSequenceStartFrame = a_frame;
			a_state.sampleSequenceClassified = a_state.sampledStallActive;
			a_state.ownerResultReported = false;
			a_state.lastOwnerCheckQpc = 0;
		}

		result.sampleSequenceTicks = Elapsed(a_now, a_state.sampleSequenceStartQpc);
		if (result.sampleSequenceTicks < a_threshold)
			return result;

		result.frameUnchangedTicks = Elapsed(a_now, a_state.lastFrameAdvanceQpc);
		const auto frameStalled =
			a_state.frameHeartbeatReady && result.frameUnchangedTicks >= a_threshold;
		if (!frameStalled)
		{
			if (!a_state.sampleSequenceClassified &&
				a_frame != a_state.sampleSequenceStartFrame)
			{
				a_state.sampleSequenceClassified = true;
				result.healthySampleSequence = true;
			}
			return result;
		}

		if (!a_state.sampledStallActive)
		{
			a_state.sampledStallActive = true;
			a_state.forcedRelease = false;
			a_state.ownerResultReported = false;
			a_state.candidateQpc = a_now;
			a_state.candidateFrame = a_frame;
			a_state.lastOwnerCheckQpc = 0;
			result.stallCandidateStarted = true;
		}
		a_state.sampleSequenceClassified = true;
		result.shouldCheckOwner = !a_state.forcedRelease;
		return result;
	}
}
