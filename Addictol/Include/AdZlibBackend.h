#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "AdZlibInflate.h"

namespace Addictol
{
	enum class ZlibBackendKind : uint8_t
	{
		Stock,
		LibDeflate
	};

	struct ZlibBackendName
	{
		std::string_view name;
		ZlibBackendKind kind;
	};

	inline constexpr std::array ZLIB_BACKEND_NAMES{
		ZlibBackendName{ "stock", ZlibBackendKind::Stock },
		ZlibBackendName{ "libdeflate", ZlibBackendKind::LibDeflate }
	};
	inline constexpr auto DEFAULT_ZLIB_BACKEND = ZlibBackendKind::LibDeflate;

	[[nodiscard]] constexpr std::string_view ZlibBackendKindName(
		ZlibBackendKind a_kind) noexcept
	{
		for (const auto& backend : ZLIB_BACKEND_NAMES)
		{
			if (backend.kind == a_kind)
				return backend.name;
		}
		return "unknown";
	}

	[[nodiscard]] constexpr std::optional<ZlibBackendKind> ParseZlibBackend(
		std::string_view a_name) noexcept
	{
		for (const auto& backend : ZLIB_BACKEND_NAMES)
		{
			if (backend.name == a_name)
				return backend.kind;
		}
		return std::nullopt;
	}

	ZlibBackendKind ResolveZlibBackendSelection(std::string_view a_name) noexcept;
	ZlibBackendKind GetSelectedZlibBackendKind() noexcept;
	void InitializeZlibBackendConfig() noexcept;

	enum class ZlibFallbackReason : uint8_t
	{
		None = 0,
		State = 1,
		Allocation = 2,
		Decode = 3,
		Commit = 4,
		Capacity = 5,
		SizeMismatch = 6,
		RequestRestart = 7
	};

	[[nodiscard]] constexpr std::string_view ZlibFallbackReasonName(
		ZlibFallbackReason a_reason) noexcept
	{
		switch (a_reason)
		{
		case ZlibFallbackReason::None:
			return "none";
		case ZlibFallbackReason::State:
			return "state";
		case ZlibFallbackReason::Allocation:
			return "allocation";
		case ZlibFallbackReason::Decode:
			return "decode";
		case ZlibFallbackReason::Commit:
			return "commit";
		case ZlibFallbackReason::Capacity:
			return "capacity";
		case ZlibFallbackReason::SizeMismatch:
			return "size-mismatch";
		case ZlibFallbackReason::RequestRestart:
			return "request-restart";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr uint32_t ZlibBackendRegistryId(
		ZlibBackendKind a_kind) noexcept
	{
		switch (a_kind)
		{
		case ZlibBackendKind::Stock:
			return 1;
		case ZlibBackendKind::LibDeflate:
			return 2;
		default:
			return 0;
		}
	}

	[[nodiscard]] constexpr uint32_t ZlibFallbackReasonRegistryId(
		ZlibFallbackReason a_reason) noexcept
	{
		return std::to_underlying(a_reason);
	}

	enum class ZlibDecodeStatus : uint8_t
	{
		Success,
		Failed
	};

	struct ZlibDecodeResult
	{
		ZlibDecodeStatus status{ ZlibDecodeStatus::Failed };
		size_t consumed{ 0 };
		size_t produced{ 0 };
	};

	// Mirrored so the classifier stays independent of the codec header.
	inline constexpr uint32_t ZLIB_CODEC_SUCCESS{ 0 };
	inline constexpr uint32_t ZLIB_CODEC_BAD_DATA{ 1 };
	inline constexpr uint32_t ZLIB_CODEC_SHORT_OUTPUT{ 2 };
	inline constexpr uint32_t ZLIB_CODEC_INSUFFICIENT_SPACE{ 3 };

	enum class ZlibExactStatus : uint8_t
	{
		Success,
		Capacity,
		SizeMismatch,
		Decode
	};

	struct ZlibExactDecode
	{
		uint32_t codecResult{ ZLIB_CODEC_BAD_DATA };
		size_t consumed{ 0 };
		size_t produced{ 0 };
	};

	[[nodiscard]] constexpr ZlibExactStatus ClassifyExactDecode(
		const ZlibExactDecode& a_decode,
		size_t a_expectedInput,
		size_t a_expectedOutput) noexcept
	{
		switch (a_decode.codecResult)
		{
		case ZLIB_CODEC_SUCCESS:
			break;
		case ZLIB_CODEC_INSUFFICIENT_SPACE:
			return ZlibExactStatus::Capacity;
		case ZLIB_CODEC_SHORT_OUTPUT:
			return ZlibExactStatus::SizeMismatch;
		default:
			return ZlibExactStatus::Decode;
		}

		// Exact on both sides; a disagreement is metadata, not corruption.
		if (a_decode.consumed != a_expectedInput || a_decode.produced != a_expectedOutput)
			return ZlibExactStatus::SizeMismatch;
		return ZlibExactStatus::Success;
	}

	[[nodiscard]] constexpr ZlibFallbackReason ExactStatusFallbackReason(
		ZlibExactStatus a_status) noexcept
	{
		switch (a_status)
		{
		case ZlibExactStatus::Capacity:
			return ZlibFallbackReason::Capacity;
		case ZlibExactStatus::SizeMismatch:
			return ZlibFallbackReason::SizeMismatch;
		case ZlibExactStatus::Decode:
			return ZlibFallbackReason::Decode;
		default:
			return ZlibFallbackReason::None;
		}
	}

	struct ZlibInflateOutcome
	{
		uint32_t primaryBackendId{ 0 };
		bool primaryAttempted{ false };
		uint64_t primaryQpc{ 0 };
		uint32_t fallbackBackendId{ 0 };
		uint32_t fallbackReasonId{ 0 };
		uint64_t fallbackQpc{ 0 };
		uint32_t servedBackendId{ 0 };
		int32_t zlibResult{ 0 };
		uint64_t totalQpc{ 0 };
		uint64_t qpcFrequency{ 0 };
		size_t consumed{ 0 };
		size_t produced{ 0 };
	};

	struct StockZlibBackend
	{
		inline static constexpr auto kind = ZlibBackendKind::Stock;
	};

	struct LibDeflateZlibBackend
	{
		inline static constexpr auto kind = ZlibBackendKind::LibDeflate;

		static bool Prepare() noexcept;
		static ZlibDecodeResult Decode(
			std::span<const uint8_t> a_input,
			std::span<uint8_t> a_output) noexcept;
		static ZlibExactDecode DecodeExact(
			std::span<const uint8_t> a_input,
			std::span<uint8_t> a_output) noexcept;
	};

	template<class F>
	decltype(auto) VisitSelectedZlibBackend(F&& a_fn)
	{
		auto fn = std::forward<F>(a_fn);
		if (GetSelectedZlibBackendKind() == ZlibBackendKind::Stock)
			return fn.template operator()<StockZlibBackend>();
		else
			return fn.template operator()<LibDeflateZlibBackend>();
	}

	template<class Clock>
	[[nodiscard]] uint64_t ReadZlibQpc(bool a_timingEnabled, Clock& a_clock) noexcept
	{
		return a_timingEnabled ? a_clock() : 0;
	}

	template<class Original, class Clock>
	void ServeStockZlib(
		ZlibInflateOutcome& a_outcome,
		bool a_isFallback,
		ZlibFallbackReason a_fallbackReason,
		ZlibInflate::Stream* a_stream,
		int32_t a_flush,
		Original& a_original,
		bool a_timingEnabled,
		Clock& a_clock) noexcept
	{
		const auto inputBefore = a_stream ? a_stream->avail_in : 0;
		const auto outputBefore = a_stream ? a_stream->avail_out : 0;
		const auto start = ReadZlibQpc(a_timingEnabled, a_clock);
		const auto result = a_original(a_stream, a_flush);
		const auto end = ReadZlibQpc(a_timingEnabled, a_clock);
		const auto inputAfter = a_stream ? a_stream->avail_in : inputBefore;
		const auto outputAfter = a_stream ? a_stream->avail_out : outputBefore;

		if (a_isFallback)
		{
			a_outcome.fallbackBackendId = ZlibBackendRegistryId(ZlibBackendKind::Stock);
			a_outcome.fallbackReasonId = ZlibFallbackReasonRegistryId(a_fallbackReason);
			a_outcome.fallbackQpc = end - start;
		}
		else
		{
			a_outcome.primaryAttempted = true;
			a_outcome.primaryQpc = end - start;
		}

		a_outcome.servedBackendId = ZlibBackendRegistryId(ZlibBackendKind::Stock);
		a_outcome.zlibResult = result;
		a_outcome.consumed = (inputAfter <= inputBefore) ? inputBefore - inputAfter : 0;
		a_outcome.produced = (outputAfter <= outputBefore) ? outputBefore - outputAfter : 0;
	}

	template<class Backend, class Original, class Clock>
	ZlibInflateOutcome ServeZlib(
		ZlibInflate::Stream* a_stream,
		int32_t a_flush,
		Original&& a_original,
		bool a_timingEnabled,
		uint64_t a_qpcFrequency,
		Clock&& a_clock) noexcept
	{
		ZlibInflateOutcome outcome{};
		outcome.primaryBackendId = ZlibBackendRegistryId(Backend::kind);
		outcome.qpcFrequency = a_timingEnabled ? a_qpcFrequency : 0;
		auto&& original = a_original;
		auto&& clock = a_clock;
		const auto totalStart = ReadZlibQpc(a_timingEnabled, clock);

		if constexpr (Backend::kind == ZlibBackendKind::Stock)
		{
			ServeStockZlib(
				outcome,
				false,
				ZlibFallbackReason::None,
				a_stream,
				a_flush,
				original,
				a_timingEnabled,
				clock);
		}
		else
		{
			if (!ZlibInflate::CanAttempt(a_stream, a_flush))
			{
				ServeStockZlib(
					outcome,
					true,
					ZlibFallbackReason::State,
					a_stream,
					a_flush,
					original,
					a_timingEnabled,
					clock);
			}
			else
			{
				const auto* expectedState = a_stream->state;
				outcome.primaryAttempted = true;
				const auto prepareStart = ReadZlibQpc(a_timingEnabled, clock);
				if (!Backend::Prepare())
				{
					const auto primaryEnd = ReadZlibQpc(a_timingEnabled, clock);
					outcome.primaryQpc = primaryEnd - prepareStart;
					ServeStockZlib(
						outcome,
						true,
						ZlibFallbackReason::Allocation,
						a_stream,
						a_flush,
						original,
						a_timingEnabled,
						clock);
				}
				else
				{
					const auto decoded = Backend::Decode(
						{ a_stream->next_in, a_stream->avail_in },
						{ a_stream->next_out, a_stream->avail_out });

					if (decoded.status != ZlibDecodeStatus::Success)
					{
						const auto primaryEnd = ReadZlibQpc(a_timingEnabled, clock);
						outcome.primaryQpc = primaryEnd - prepareStart;
						// Raw and gzip streams fail the zlib probe without reading private wrap.
						ServeStockZlib(
							outcome,
							true,
							ZlibFallbackReason::Decode,
							a_stream,
							a_flush,
							original,
							a_timingEnabled,
							clock);
					}
					else
					{
						const auto committed = ZlibInflate::CommitCompletedStream(
							a_stream, expectedState, decoded.consumed, decoded.produced);
						const auto primaryEnd = ReadZlibQpc(a_timingEnabled, clock);
						outcome.primaryQpc = primaryEnd - prepareStart;

						if (!committed)
						{
							// The codec may write output; stock rewrites it from unchanged stream state.
							ServeStockZlib(
								outcome,
								true,
								ZlibFallbackReason::Commit,
								a_stream,
								a_flush,
								original,
								a_timingEnabled,
								clock);
						}
						else
						{
							outcome.servedBackendId = ZlibBackendRegistryId(Backend::kind);
							outcome.zlibResult = ZlibInflate::Z_STREAM_END;
							outcome.consumed = decoded.consumed;
							outcome.produced = decoded.produced;
						}
					}
				}
			}
		}

		const auto totalEnd = ReadZlibQpc(a_timingEnabled, clock);
		outcome.totalQpc = totalEnd - totalStart;
		return outcome;
	}
}
