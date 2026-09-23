#include <Zlib/AdZlibOperationProfile.h>
#include <Core/AdClock.h>
#include <Telemetry/AdTelemetryHub.h>

namespace Addictol
{
	namespace
	{
		OperationProfileSourceOwner s_zlibProfile;
	}

	OperationProfileSource* ZlibOperationProfile() noexcept
	{
		return s_zlibProfile.Get();
	}

	ZlibProfileStreamTracker& ZlibOperationStreamTracker() noexcept
	{
		static ZlibProfileStreamTracker tracker;
		return tracker;
	}

	void ZlibStreamProfile::Retire(ZlibStreamProfile& a_profile,
		const ZlibStreamProgress& a_progress, ZlibStreamResult a_result) noexcept
	{
		a_profile.source->End(std::move(a_profile.token), a_progress.totalOutput,
			ZlibStreamProfileResult(a_profile.backend, a_profile.failure, a_result));
	}

	void ZlibStreamProfileObserver::Before(const ZlibInflate::Stream* a_stream,
		ZlibFallbackReason a_reason, const ZlibInflateOutcome& a_outcome) noexcept
	{
		if (!a_stream || !a_stream->state)
			return;
		m_input = ZlibStreamInput::Read(*a_stream);
		m_follow = a_reason == ZlibFallbackReason::State;
		if (!m_input.start)
			return;

		const auto failure = ClassifyZlibDecodeFailure(
			a_outcome.primaryCodecResult, a_outcome.hasZlibHeader);
		if (m_source && a_reason == ZlibFallbackReason::Decode && IsTrackedZlibDecodeFailure(failure))
		{
			m_tracker.DiscardIf([](const ZlibStreamProfile& a_profile) {
				return !a_profile.source->IsCurrent(a_profile.token);
			}, ZlibStreamProfile::Retire);
			auto token = m_source->Begin(ZlibStreamProfileAdmission(m_backend));
			if (token)
			{
				m_tracker.Start(m_input,
					ZlibStreamProfile{ m_source, std::move(token), m_backend, failure },
					ZlibStreamProfile::Retire);
				m_follow = true;
				return;
			}
		}
		m_tracker.Abandon(m_input.state, ZlibStreamProfile::Retire);
	}

	void ZlibStreamProfileObserver::After(const ZlibInflate::Stream* a_stream, int32_t a_result) noexcept
	{
		if (m_follow)
			m_tracker.FinishCall(m_input, a_stream->total_in, a_stream->total_out,
				a_result, ZlibStreamProfile::Retire);
	}

	bool InitializeZlibOperationProfile(TelemetryHub& a_hub, ZlibBackendKind a_backend) noexcept
	{
		if (s_zlibProfile.Get())
			return true;
		ScopedOperationProfileSuppression suppression;
		const std::array labels{
			OperationProfileMetadataLabel{ "selected_backend", ZlibBackendKindName(a_backend) }
		};
		OperationProfileConfiguration configuration{};
		configuration.sourceId = "decompression";
		configuration.sourceName = "Zlib inflate operations";
		configuration.descriptors = kZlibProfileDescriptors;
		configuration.metadataLabels = labels;
		configuration.recordCapacity = kZlibProfileRecordCapacity;
		return s_zlibProfile.Register(a_hub, configuration);
	}
}
