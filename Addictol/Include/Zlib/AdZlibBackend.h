#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <Zlib/AdZlibInflate.h>

#define ADDICTOL_ZLIB_BACKENDS(X) \
	X(Stock, "stock", NoWholeInflateDecoder, void) \
	X(Zlib, "zlib", NoWholeInflateDecoder, ZlibDecoder) \
	X(ZlibNg, "zlib-ng", NoWholeInflateDecoder, ZlibNgDecoder) \
	X(Isal, "isa-l", NoWholeInflateDecoder, IsalDecoder) \
	X(HybridZlibNg, "hybrid-zlib-ng", LibDeflateZlibBackend, ZlibNgDecoder) \
	X(HybridIsal, "hybrid-isa-l", LibDeflateZlibBackend, IsalDecoder)

namespace Addictol
{
	enum class ZlibBackendKind : uint8_t
	{
#define ZLIB_KIND(Kind, Name, Whole, Streaming) Kind,
		ADDICTOL_ZLIB_BACKENDS(ZLIB_KIND)
#undef ZLIB_KIND
	};
	struct ZlibBackendName
	{
		std::string_view name;
		ZlibBackendKind kind;
		std::string_view whole, streaming;
	};
	inline constexpr std::array ZLIB_BACKEND_NAMES{
#define ZLIB_NAME(Kind, Name, Whole, Streaming) ZlibBackendName{ Name, ZlibBackendKind::Kind, #Whole, #Streaming },
		ADDICTOL_ZLIB_BACKENDS(ZLIB_NAME)
#undef ZLIB_NAME
	};
	inline constexpr auto DEFAULT_ZLIB_BACKEND = ZlibBackendKind::HybridZlibNg;
	constexpr std::string_view ZlibBackendKindName(ZlibBackendKind a_kind) noexcept
	{
		for (const auto& row : ZLIB_BACKEND_NAMES)
			if (row.kind == a_kind) return row.name;
		return "unknown";
	}
	constexpr std::optional<ZlibBackendKind> ParseZlibBackend(std::string_view a_name) noexcept
	{
		for (const auto& row : ZLIB_BACKEND_NAMES)
			if (row.name == a_name) return row.kind;
		return std::nullopt;
	}
	constexpr uint32_t ZlibBackendRegistryId(ZlibBackendKind a_kind) noexcept
	{
		return static_cast<uint32_t>(a_kind) + 1;
	}
	ZlibBackendKind ResolveZlibBackendSelection(std::string_view) noexcept;
	ZlibBackendKind GetSelectedZlibBackendKind() noexcept;
	void InitializeZlibBackendConfig() noexcept;

	enum class ZlibOwnedPolicy : uint8_t { Undecided, Streaming, Whole, Buffered, Done };
	enum class ZlibFallbackReason : uint8_t { None, NoWhole, Format, Request, Allocation, Decode, Capacity };
	struct ZlibFallbackReasonEntry { std::string_view name; ZlibFallbackReason reason; };
	inline constexpr std::array ZLIB_FALLBACK_REASONS{
		ZlibFallbackReasonEntry{ "none", ZlibFallbackReason::None },
		ZlibFallbackReasonEntry{ "no-whole", ZlibFallbackReason::NoWhole },
		ZlibFallbackReasonEntry{ "format", ZlibFallbackReason::Format },
		ZlibFallbackReasonEntry{ "request", ZlibFallbackReason::Request },
		ZlibFallbackReasonEntry{ "allocation", ZlibFallbackReason::Allocation },
		ZlibFallbackReasonEntry{ "decode", ZlibFallbackReason::Decode },
		ZlibFallbackReasonEntry{ "capacity", ZlibFallbackReason::Capacity }
	};
	constexpr std::string_view ZlibFallbackReasonName(ZlibFallbackReason a_reason) noexcept
	{
		for (const auto& row : ZLIB_FALLBACK_REASONS)
			if (row.reason == a_reason) return row.name;
		return "unknown";
	}
	constexpr uint32_t ZlibFallbackReasonRegistryId(ZlibFallbackReason a_reason) noexcept { return std::to_underlying(a_reason); }
	enum class ZlibDecodeStatus : uint8_t { Success, BadData, InsufficientSpace };
	struct ZlibDecodeResult
	{
		ZlibDecodeStatus status{ ZlibDecodeStatus::BadData };
		size_t consumed{}, produced{};
		uint32_t codecResult{ UINT32_MAX };
	};
	inline constexpr uint32_t ZLIB_CODEC_SUCCESS = 0, ZLIB_CODEC_BAD_DATA = 1, ZLIB_CODEC_SHORT_OUTPUT = 2, ZLIB_CODEC_INSUFFICIENT_SPACE = 3;
	enum class ZlibDecodeFailure : uint8_t { BadHeader, BadData, InsufficientSpace, ShortOutput, Other };
	struct ZlibDecodeFailureEntry { std::string_view name; ZlibDecodeFailure failure; };
	inline constexpr std::array ZLIB_DECODE_FAILURES{
		ZlibDecodeFailureEntry{ "bad_header", ZlibDecodeFailure::BadHeader },
		ZlibDecodeFailureEntry{ "bad_data", ZlibDecodeFailure::BadData },
		ZlibDecodeFailureEntry{ "insufficient_space", ZlibDecodeFailure::InsufficientSpace },
		ZlibDecodeFailureEntry{ "short_output", ZlibDecodeFailure::ShortOutput },
		ZlibDecodeFailureEntry{ "other", ZlibDecodeFailure::Other }
	};
	constexpr ZlibDecodeFailure ClassifyZlibDecodeFailure(uint32_t a_result, bool a_header) noexcept
	{
		if (!a_header) return ZlibDecodeFailure::BadHeader;
		switch (a_result)
		{
		case ZLIB_CODEC_BAD_DATA: return ZlibDecodeFailure::BadData;
		case ZLIB_CODEC_INSUFFICIENT_SPACE: return ZlibDecodeFailure::InsufficientSpace;
		case ZLIB_CODEC_SHORT_OUTPUT: return ZlibDecodeFailure::ShortOutput;
		default: return ZlibDecodeFailure::Other;
		}
	}
	struct ZlibInflateOutcome
	{
		uint32_t fallbackReasonId{};
		int32_t zlibResult{};
		uint64_t totalQpc{};
		size_t consumed{}, produced{};
		uint32_t primaryCodecResult{ UINT32_MAX };
		bool hasZlibHeader{ true };
		ZlibOwnedPolicy policy{ ZlibOwnedPolicy::Streaming };
	};
	struct LibDeflateZlibBackend
	{
		static ZlibDecodeResult Decode(std::span<const uint8_t>, std::span<uint8_t>) noexcept;
	};
	struct ZlibCallObserver
	{
		void Before(const ZlibInflate::Stream*, ZlibFallbackReason, const ZlibInflateOutcome&) const noexcept {}
		void After(const ZlibInflate::Stream*, int32_t) const noexcept {}
	};
}
