#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <Zlib/AdZlibInflate.h>

#define ADDICTOL_ZLIB_BACKENDS(X) \
	X(Stock, "stock", NoWholeInflateDecoder, void, "none", "vanilla") \
	X(Zlib, "zlib", NoWholeInflateDecoder, ZlibDecoder, "none", "zlib") \
	X(ZlibNg, "zlib-ng", NoWholeInflateDecoder, ZlibNgDecoder, "none", "zlib-ng") \
	X(Isal, "isa-l", NoWholeInflateDecoder, IsalDecoder, "none", "isa-l") \
	X(HybridZlibNg, "hybrid-zlib-ng", LibDeflateZlibBackend, ZlibNgDecoder, "libdeflate", "zlib-ng") \
	X(HybridIsal, "hybrid-isa-l", LibDeflateZlibBackend, IsalDecoder, "libdeflate", "isa-l")

namespace Addictol
{
	enum class ZlibBackendKind : uint8_t
	{
#define ZLIB_KIND(Kind, Name, Whole, Streaming, WholeName, StreamingName) Kind,
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
#define ZLIB_NAME(Kind, Name, Whole, Streaming, WholeName, StreamingName) ZlibBackendName{ Name, ZlibBackendKind::Kind, WholeName, StreamingName },
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
	struct ZlibInflateOutcome
	{
		uint32_t fallbackReasonId{};
		int32_t zlibResult{};
		uint64_t totalQpc{};
		size_t consumed{}, produced{};
		ZlibOwnedPolicy policy{ ZlibOwnedPolicy::Streaming };
	};
	struct LibDeflateZlibBackend
	{
		static ZlibDecodeResult Decode(std::span<const uint8_t>, std::span<uint8_t>) noexcept;
	};
}
