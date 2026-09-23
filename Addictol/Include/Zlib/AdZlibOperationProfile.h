#pragma once

#include <Telemetry/AdOperationProfileTable.h>
#include <Zlib/AdZlibBackend.h>

namespace Addictol
{
	namespace ZlibProfileDetail
	{
		inline constexpr size_t kDescriptorsPerBackend{ 1 + ZLIB_FALLBACK_REASONS.size() };
		inline constexpr auto kNames = [] {
			std::array<OperationProfileNames, ZLIB_BACKEND_NAMES.size() * kDescriptorsPerBackend> names{};
			size_t index{ 0 };
			for (const auto& backend : ZLIB_BACKEND_NAMES)
			{
				OperationProfileName group;
				group.Append("zlib.inflate.");
				group.Append(backend.name);
				names[index++] = MakeOperationProfileNames(group, "requested");
				for (const auto& reason : ZLIB_FALLBACK_REASONS)
				{
					OperationProfileName result;
					if (reason.reason == ZlibFallbackReason::None)
						result.Append("primary");
					else
					{
						result.Append("fallback.");
						result.Append(reason.name);
					}
					names[index++] = MakeOperationProfileNames(group, result.View());
				}
			}
			return names;
		}();
	}

	inline constexpr auto kZlibProfileDescriptors = MakeOperationProfileDescriptors(ZlibProfileDetail::kNames, 1);
	// Load bursts reached ~87k inflates within one collection interval.
	inline constexpr size_t kZlibProfileRecordCapacity{ 131072 };

	[[nodiscard]] constexpr uint32_t ZlibProfileAdmission(ZlibBackendKind a_backend) noexcept
	{
		for (size_t index = 0; index < ZLIB_BACKEND_NAMES.size(); ++index)
		{
			if (ZLIB_BACKEND_NAMES[index].kind == a_backend)
				return static_cast<uint32_t>(index * ZlibProfileDetail::kDescriptorsPerBackend);
		}
		return UINT32_MAX;
	}

	[[nodiscard]] constexpr uint32_t ZlibProfileResult(
		ZlibBackendKind a_backend, const ZlibInflateOutcome& a_outcome) noexcept
	{
		const auto admission = ZlibProfileAdmission(a_backend);
		if (admission == UINT32_MAX)
			return UINT32_MAX;
		for (size_t index = 0; index < ZLIB_FALLBACK_REASONS.size(); ++index)
		{
			if (ZlibFallbackReasonRegistryId(ZLIB_FALLBACK_REASONS[index].reason) == a_outcome.fallbackReasonId)
				return admission + 1 + static_cast<uint32_t>(index);
		}
		return UINT32_MAX;
	}

	[[nodiscard]] OperationProfileSource* ZlibOperationProfile() noexcept;
	[[nodiscard]] bool InitializeZlibOperationProfile(TelemetryHub& a_hub, ZlibBackendKind a_backend) noexcept;

	template<class Backend, bool Enabled, class Serve>
	[[nodiscard]] ZlibInflateOutcome ServeProfiledZlib(
		OperationProfileSource* a_source, Serve&& a_serve) noexcept
	{
		OperationProfileConsumer<Enabled> profile{ a_source };
		auto token = profile.Begin(ZlibProfileAdmission(Backend::kind));
		const auto outcome = a_serve();
		if constexpr (Enabled)
			profile.End(std::move(token), outcome.produced, ZlibProfileResult(Backend::kind, outcome));
		return outcome;
	}
}
