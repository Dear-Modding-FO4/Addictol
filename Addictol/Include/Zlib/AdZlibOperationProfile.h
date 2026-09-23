#pragma once

#include <Telemetry/AdOperationProfileTable.h>
#include <Zlib/AdZlibBackend.h>

namespace Addictol
{
	namespace ZlibProfileDetail
	{
		inline constexpr size_t kDescriptorsPerBackend = 2 + ZLIB_FALLBACK_REASONS.size() - 1;
		inline constexpr auto kNames = [] {
			std::array<OperationProfileNames, ZLIB_BACKEND_NAMES.size() * kDescriptorsPerBackend> names{};
			size_t index{};
			for (const auto& backend : ZLIB_BACKEND_NAMES)
			{
				OperationProfileName group;
				group.Append("zlib.stream.");
				group.Append(backend.name);
				names[index++] = MakeOperationProfileNames(group, "requested");
				names[index++] = MakeOperationProfileNames(group, "whole");
				for (const auto& reason : ZLIB_FALLBACK_REASONS)
				{
					if (reason.reason == ZlibFallbackReason::None)
						continue;
					OperationProfileName result;
					result.Append("streaming.");
					result.Append(reason.name);
					names[index++] = MakeOperationProfileNames(group, result.View());
				}
			}
			return names;
		}();
	}

	inline constexpr auto kZlibProfileDescriptors = MakeOperationProfileDescriptors(ZlibProfileDetail::kNames, 1);
	inline constexpr size_t kZlibProfileRecordCapacity{ 131072 };

	constexpr uint32_t ZlibProfileAdmission(ZlibBackendKind a_backend) noexcept
	{
		for (size_t index = 0; index < ZLIB_BACKEND_NAMES.size(); ++index)
			if (ZLIB_BACKEND_NAMES[index].kind == a_backend)
				return static_cast<uint32_t>(index * ZlibProfileDetail::kDescriptorsPerBackend);
		return UINT32_MAX;
	}

	constexpr uint32_t ZlibProfileResult(ZlibBackendKind a_backend, ZlibOwnedPolicy a_policy,
		ZlibFallbackReason a_reason) noexcept
	{
		const auto admission = ZlibProfileAdmission(a_backend);
		if (admission == UINT32_MAX)
			return UINT32_MAX;
		if (a_policy == ZlibOwnedPolicy::Whole) return admission + 1;
		auto result = admission + 2;
		for (const auto& reason : ZLIB_FALLBACK_REASONS)
		{
			if (reason.reason == ZlibFallbackReason::None) continue;
			if (reason.reason == a_reason) return result;
			++result;
		}
		return UINT32_MAX;
	}

	OperationProfileSource* ZlibOperationProfile() noexcept;
	bool InitializeZlibOperationProfile(TelemetryHub&, ZlibBackendKind) noexcept;

	struct NoZlibStreamProfile
	{
		inline static constexpr bool enabled = false;
	};

	struct ZlibProfileSource
	{
		static OperationProfileSource* Get() noexcept { return ZlibOperationProfile(); }
	};

	template<ZlibBackendKind Backend, class Source = ZlibProfileSource>
	struct OwnedZlibStreamProfile
	{
		inline static constexpr bool enabled = true;

		template<class State>
		static void Begin(State& a_state) noexcept
		{
			// An engaged, empty token also remembers an unsampled or already-completed lifetime.
			if (a_state.profileToken)
				return;
			a_state.profileSource = Source::Get();
			a_state.profileToken.emplace(a_state.profileSource ?
				a_state.profileSource->BeginAccumulated(ZlibProfileAdmission(Backend)) : OperationProfileToken{});
		}

		template<class State, class Function>
		static int32_t Measure(State& a_state, Function&& a_inflate) noexcept
		{
			Begin(a_state);
			if (!*a_state.profileToken)
				return a_inflate();
			const auto start = a_state.profileSource->ReadClock();
			const auto result = a_inflate();
			const auto finish = a_state.profileSource->ReadClock();
			a_state.profileElapsedQpc += finish > start ? finish - start : 0;
			return result;
		}

		template<class State>
		static void End(State& a_state, uint64_t a_bytes) noexcept
		{
			if (a_state.profileToken && *a_state.profileToken)
				a_state.profileSource->EndWithDuration(std::move(*a_state.profileToken), a_state.profileElapsedQpc, a_bytes,
					ZlibProfileResult(Backend, a_state.outcomePolicy, a_state.fallbackReason));
		}
	};
}
