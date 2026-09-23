#pragma once

#include <Telemetry/AdOperationProfileTable.h>
#include <Zlib/AdZlibBackend.h>
#include <Zlib/AdZlibStreamTracker.h>

namespace Addictol
{
	[[nodiscard]] constexpr bool IsTrackedZlibDecodeFailure(ZlibDecodeFailure a_failure) noexcept
	{
		return a_failure == ZlibDecodeFailure::InsufficientSpace ||
			a_failure == ZlibDecodeFailure::BadData;
	}

	namespace ZlibProfileDetail
	{
		inline constexpr size_t kDescriptorsPerBackend{
			1 + ZLIB_FALLBACK_REASONS.size() - 1 + ZLIB_DECODE_FAILURES.size()
		};
		inline constexpr size_t kStreamDescriptorsPerBackend = [] {
			size_t count{ 1 };
			for (const auto& failure : ZLIB_DECODE_FAILURES)
			{
				if (IsTrackedZlibDecodeFailure(failure.failure))
					count += ZLIB_STREAM_RESULTS.size();
			}
			return count;
		}();
		inline constexpr size_t kStreamOffset{ ZLIB_BACKEND_NAMES.size() * kDescriptorsPerBackend };
		inline constexpr auto kNames = [] {
			std::array<OperationProfileNames, kStreamOffset +
				ZLIB_BACKEND_NAMES.size() * kStreamDescriptorsPerBackend> names{};
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
					if (reason.reason == ZlibFallbackReason::Decode)
					{
						for (const auto& failure : ZLIB_DECODE_FAILURES)
						{
							auto decodeResult = result;
							decodeResult.Append(".");
							decodeResult.Append(failure.name);
							names[index++] = MakeOperationProfileNames(group, decodeResult.View());
						}
					}
					else
						names[index++] = MakeOperationProfileNames(group, result.View());
				}
			}
			for (const auto& backend : ZLIB_BACKEND_NAMES)
			{
				OperationProfileName group;
				group.Append("zlib.stream.");
				group.Append(backend.name);
				names[index++] = MakeOperationProfileNames(group, "requested");
				for (const auto& failure : ZLIB_DECODE_FAILURES)
				{
					if (!IsTrackedZlibDecodeFailure(failure.failure))
						continue;
					for (const auto& [result, name] : ZLIB_STREAM_RESULTS)
					{
						OperationProfileName classification;
						classification.Append(failure.name);
						classification.Append(".");
						classification.Append(name);
						names[index++] = MakeOperationProfileNames(group, classification.View());
					}
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
		auto result = admission + 1;
		for (const auto& reason : ZLIB_FALLBACK_REASONS)
		{
			const auto matches = ZlibFallbackReasonRegistryId(reason.reason) == a_outcome.fallbackReasonId;
			if (reason.reason == ZlibFallbackReason::Decode)
			{
				const auto failure = ClassifyZlibDecodeFailure(
					a_outcome.primaryCodecResult, a_outcome.hasZlibHeader);
				for (const auto& entry : ZLIB_DECODE_FAILURES)
				{
					if (matches && entry.failure == failure)
						return result;
					++result;
				}
			}
			else
			{
				if (matches)
					return result;
				++result;
			}
		}
		return UINT32_MAX;
	}

	[[nodiscard]] OperationProfileSource* ZlibOperationProfile() noexcept;
	[[nodiscard]] bool InitializeZlibOperationProfile(TelemetryHub& a_hub, ZlibBackendKind a_backend) noexcept;

	[[nodiscard]] constexpr uint32_t ZlibStreamProfileAdmission(ZlibBackendKind a_backend) noexcept
	{
		for (size_t index = 0; index < ZLIB_BACKEND_NAMES.size(); ++index)
		{
			if (ZLIB_BACKEND_NAMES[index].kind == a_backend)
				return static_cast<uint32_t>(ZlibProfileDetail::kStreamOffset +
					index * ZlibProfileDetail::kStreamDescriptorsPerBackend);
		}
		return UINT32_MAX;
	}

	[[nodiscard]] constexpr uint32_t ZlibStreamProfileResult(ZlibBackendKind a_backend,
		ZlibDecodeFailure a_failure, ZlibStreamResult a_result) noexcept
	{
		const auto admission = ZlibStreamProfileAdmission(a_backend);
		if (admission == UINT32_MAX)
			return UINT32_MAX;
		auto index = admission + 1;
		for (const auto& failure : ZLIB_DECODE_FAILURES)
		{
			if (!IsTrackedZlibDecodeFailure(failure.failure))
				continue;
			for (const auto& [result, name] : ZLIB_STREAM_RESULTS)
			{
				if (failure.failure == a_failure && result == a_result)
					return index;
				++index;
			}
		}
		return UINT32_MAX;
	}

	struct ZlibStreamProfile
	{
		OperationProfileSource* source;
		OperationProfileToken token;
		ZlibBackendKind backend;
		ZlibDecodeFailure failure;

		static void Retire(ZlibStreamProfile& a_profile, const ZlibStreamProgress& a_progress,
			ZlibStreamResult a_result) noexcept;
	};

	using ZlibProfileStreamTracker = ZlibStreamTracker<ZlibStreamProfile>;
	[[nodiscard]] ZlibProfileStreamTracker& ZlibOperationStreamTracker() noexcept;

	class ZlibStreamProfileObserver
	{
	public:
		ZlibStreamProfileObserver(OperationProfileSource* a_source, ZlibBackendKind a_backend,
			ZlibProfileStreamTracker& a_tracker) noexcept :
			m_source(a_source), m_backend(a_backend), m_tracker(a_tracker)
		{}

		void Before(const ZlibInflate::Stream* a_stream, ZlibFallbackReason a_reason,
			const ZlibInflateOutcome& a_outcome) noexcept;
		void After(const ZlibInflate::Stream* a_stream, int32_t a_result) noexcept;

	private:
		OperationProfileSource* m_source;
		ZlibBackendKind m_backend;
		ZlibProfileStreamTracker& m_tracker;
		ZlibStreamInput m_input{};
		bool m_follow{ false };
	};

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

	template<class Backend, bool Enabled, class Serve>
	[[nodiscard]] ZlibInflateOutcome ServeProfiledZlibDispatch(
		OperationProfileSource* a_source, Serve&& a_serve,
		ZlibProfileStreamTracker* a_tracker = nullptr) noexcept
	{
		return ServeProfiledZlib<Backend, Enabled>(a_source, [&] {
			if constexpr (Enabled)
			{
				ZlibStreamProfileObserver observer{
					a_source, Backend::kind, a_tracker ? *a_tracker : ZlibOperationStreamTracker()
				};
				return a_serve(observer);
			}
			else
				return a_serve(ZlibCallObserver{});
		});
	}
}
