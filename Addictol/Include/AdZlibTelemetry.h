#pragma once

#include <AdTelemetryHub.h>
#include <AdZlibBackend.h>

namespace Addictol::TelemetryDetail
{
	template<class Backend, class Original, class Clock, class ThreadReader, class Recorder>
	[[nodiscard]] ZlibInflateOutcome ServeTelemetryZlib(
		ZlibInflate::Stream* a_stream,
		int32_t a_flush,
		Original&& a_original,
		Clock&& a_clock,
		ThreadReader&& a_threadReader,
		Recorder&& a_recorder) noexcept
	{
		const auto telemetryEnabled = Telemetry::EnabledRelaxed();
		auto&& original = a_original;
		auto&& clock = a_clock;
		const auto outcome = ServeZlib<Backend>(
			a_stream,
			a_flush,
			original,
			telemetryEnabled,
			telemetryEnabled ? QpcFrequency() : 0,
			clock);
		const auto recordEnabled =
			telemetryEnabled && Telemetry::EnabledRelaxed();
		auto&& threadReader = a_threadReader;
		const auto currentThreadId =
			recordEnabled ? threadReader() : 0;
		auto&& recorder = a_recorder;
		recorder(outcome, recordEnabled, currentThreadId);
		return outcome;
	}
}
