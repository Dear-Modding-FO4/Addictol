#pragma once

#include <Core/AdClock.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Zlib/AdOwnedInflate.h>

namespace Addictol::TelemetryDetail
{
	template<class Owned, class Clock, class ThreadReader, class Recorder>
	ZlibInflateOutcome ServeTelemetryZlib(ZlibInflate::Stream* a_stream, int32_t a_flush,
		Clock&& a_clock, ThreadReader&& a_threadReader, Recorder&& a_recorder) noexcept
	{
		const bool enabled = Telemetry::EnabledRelaxed();
		const auto before = enabled ? a_clock() : 0;
		const auto input = a_stream->total_in, output = a_stream->total_out;
		ZlibInflateOutcome outcome{};
		outcome.zlibResult = Owned::Inflate(a_stream, a_flush);
		outcome.totalQpc = enabled ? a_clock() - before : 0;
		outcome.consumed = static_cast<uint32_t>(a_stream->total_in - input);
		outcome.produced = static_cast<uint32_t>(a_stream->total_out - output);
		if (const auto* state = ZlibOwnedState::Find(a_stream))
		{
			outcome.policy = state->outcomePolicy;
			outcome.fallbackReasonId = ZlibFallbackReasonRegistryId(state->fallbackReason);
			outcome.primaryCodecResult = state->codecResult;
		}
		const bool record = enabled && Telemetry::EnabledRelaxed();
		a_recorder(outcome, record, record ? a_threadReader() : 0);
		return outcome;
	}
}
