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
		return s_zlibProfile.Register(a_hub, configuration);
	}
}
