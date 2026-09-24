#include <Memory/AdProfiledHeap.h>
#include <Core/AdClock.h>
#include <Core/Settings/AdSettings.h>
#include <Telemetry/AdTelemetryHub.h>

namespace Addictol
{
	namespace
	{
		OperationProfileSourceOwner s_heapProfile;
	}

	OperationProfileSource* HeapOperationProfile() noexcept
	{
		return s_heapProfile.Get();
	}

	bool PrepareHeapOperationProfile() noexcept
	{
		if (!bTelemetryOperationProfiling.GetValue())
			return false;
		const auto selected = HeapKindName(GetSelectedHeapKind());
		const auto initialized = InitializeHeapOperationProfile(
			Telemetry::Hub(), selected,
			bPatchesSmallBlockAllocatorUseSelectedHeap.GetValue() ? selected : "visper",
			AD_USE_VISPER_AS_DEFAULT ? "visper" : HeapKindName(HeapKind::Voltek));
		if (!initialized)
			REX::WARN("Allocator operation profiling registration failed; leaving heap dispatch uninstrumented.");
		return initialized;
	}

	bool InitializeHeapOperationProfile(
		TelemetryHub& a_hub, std::string_view a_selectedHeap, std::string_view a_smallBlockHeap,
		std::string_view a_scaleformHeap) noexcept
	{
		if (s_heapProfile.Get())
			return true;
		ScopedOperationProfileSuppression suppression;
		const std::array labels{
			OperationProfileMetadataLabel{ "selected_backend", a_selectedHeap },
			OperationProfileMetadataLabel{ "small_block_backend", a_smallBlockHeap },
			OperationProfileMetadataLabel{ "scaleform_backend", a_scaleformHeap }
		};
		OperationProfileConfiguration configuration{};
		configuration.sourceId = "allocator";
		configuration.sourceName = "Allocator operations";
		configuration.descriptors = kHeapProfileDescriptors;
		configuration.durationBuckets = kHeapProfileDurationBuckets;
		configuration.metadataLabels = labels;
		configuration.recordCapacity = kHeapProfileRecordCapacity;
		return s_heapProfile.Register(a_hub, configuration);
	}
}
