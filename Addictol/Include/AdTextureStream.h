#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>

#include "AdProfilerBA2Schema.h"
#include "AdZlibInflate.h"

namespace Addictol::TextureStream
{
	// uncompressedSize is the archive's exact size for one mip run.
	struct ChunkDesc
	{
		uint64_t dataFileOffset;
		uint32_t compressedSize;
		uint32_t uncompressedSize;
		uint16_t mipFirst;
		uint16_t mipLast;
		uint32_t padding;
	};

	static_assert(offsetof(ChunkDesc, compressedSize) == 0x08);
	static_assert(offsetof(ChunkDesc, uncompressedSize) == 0x0C);
	static_assert(sizeof(ChunkDesc) == 0x18);

	struct ResidentState
	{
		const uint8_t* compressedBase;
		const uint8_t* compressedCursor;
	};

	static_assert(offsetof(ResidentState, compressedCursor) == 0x08);

	struct Detail
	{
		const void* vtable;
		uint8_t directFlag;
		uint8_t reserved09[7];
		ZlibInflate::Stream stream;
	};

	static_assert(offsetof(Detail, directFlag) == 0x08);
	static_assert(sizeof(Detail::directFlag) == 1);
	static_assert(offsetof(Detail, stream) == 0x10);

	struct Stream
	{
		Detail* detail;
		ChunkDesc* chunks;
		uint32_t* nominalSizes;
		ResidentState* resident;
		void* alternate;
		void* reserved28;
		std::byte* shadowBuffer;
		uint32_t shadowSize;
		uint32_t reserved3C;
		uint32_t count;
		uint32_t index;
		uint32_t mip;
		uint32_t first;
		uint32_t last;
	};

	static_assert(offsetof(Stream, chunks) == 0x08);
	static_assert(offsetof(Stream, nominalSizes) == 0x10);
	static_assert(offsetof(Stream, resident) == 0x18);
	static_assert(offsetof(Stream, alternate) == 0x20);
	static_assert(offsetof(Stream, shadowBuffer) == 0x30);
	static_assert(offsetof(Stream, shadowSize) == 0x38);
	static_assert(sizeof(Stream::shadowSize) == 4);
	static_assert(offsetof(Stream, reserved3C) == 0x3C);
	static_assert(offsetof(Stream, count) == 0x40);
	static_assert(offsetof(Stream, index) == 0x44);
	static_assert(offsetof(Stream, first) == 0x4C);
	static_assert(offsetof(Stream, last) == 0x50);

	// Engine byte-compares this mode; bytes above it carry no contract.
	inline constexpr uint8_t DETAIL_DIRECT = 1;
	inline constexpr uint32_t MAX_CHUNK_COUNT = 255;

	struct RuntimeIds
	{
		uint64_t og{ 0 };
		uint64_t ng{ 0 };
		uint64_t ae{ 0 };

		[[nodiscard]] constexpr bool Shared() const noexcept { return og == ng && ng == ae && og; }
		[[nodiscard]] constexpr bool OgOnly() const noexcept { return og && !ng && !ae; }
	};

	inline constexpr RuntimeIds SEAM_ID{ 916914, 2275483, 2275483 };
	inline constexpr RuntimeIds DETAIL_VTABLE_ID{ 436461, 436461, 436461 };
	inline constexpr RuntimeIds STREAMING_CALLER_ID{ 1395021, 2277269, 2277269 };
	inline constexpr RuntimeIds ARRAY_SLICE_CALLER_ID{ 9669, 2277272, 2277272 };
	inline constexpr RuntimeIds OG_RESIDENT_INNER_ID{ 371154, 0, 0 };
	inline constexpr RuntimeIds OG_ALTERNATE_INNER_ID{ 129694, 0, 0 };

	namespace Guards
	{
		// AE/NG enter the resident path directly; the index reset pins the entry.
		inline constexpr std::initializer_list<uint8_t> MODERN_ENTRY{
			0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x4C,
			0x8B, 0x71, 0x18, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x4D,
			0x85, 0xF6, 0x0F, 0x84, 0x1F, 0x01, 0x00, 0x00, 0xC7, 0x41,
			0x44, 0x00, 0x00, 0x00, 0x00
		};

		// OG enters a dispatcher that tail-jumps to the resident or the alternate loop.
		inline constexpr std::initializer_list<uint8_t> OG_DISPATCHER{
			0x48, 0x8B, 0x41, 0x18, 0x48, 0x85, 0xC0, 0x74, 0x0E, 0x4C,
			0x8B, 0xC2, 0x48, 0x8B, 0xD1, 0x48, 0x8B, 0xC8, 0xE9, 0x69,
			0x01, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x20, 0x48, 0x85, 0xC0,
			0x74, 0x0E, 0x4C, 0x8B, 0xC2, 0x48, 0x8B, 0xD1, 0x48, 0x8B,
			0xC8, 0xE9, 0xB2, 0x06, 0x00, 0x00, 0xF3, 0xC3
		};

		// Relocation-free prologue, up to the first branch.
		inline constexpr std::initializer_list<uint8_t> OG_RESIDENT_INNER{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
			0x4C, 0x89, 0x44, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48,
			0x83, 0xEC, 0x20, 0xC7, 0x42, 0x44, 0x00, 0x00, 0x00, 0x00,
			0x48, 0x8B, 0x01, 0x48, 0x8B, 0xDA, 0x48, 0x89, 0x41, 0x08,
			0x8B, 0x52, 0x44, 0x49, 0x8B, 0xF8, 0x44, 0x8B, 0x43, 0x4C,
			0x4C, 0x8B, 0xF1, 0x40, 0xB6, 0x01, 0x41, 0x3B, 0xD0
		};

		// Relocation-free prologue, up to the first branch.
		inline constexpr std::initializer_list<uint8_t> OG_ALTERNATE_INNER{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
			0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
			0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x44, 0x8B, 0x4A, 0x4C,
			0x33, 0xED, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x89, 0x6A,
			0x44, 0x45, 0x85, 0xC9
		};

		inline constexpr size_t MODERN_ENTRY_OVERWRITE = 5;
		inline constexpr size_t OG_DISPATCHER_OVERWRITE = 7;
	}

	[[nodiscard]] inline bool MatchesBytes(
		std::span<const uint8_t> a_code,
		size_t a_offset,
		std::span<const uint8_t> a_expected) noexcept
	{
		return a_offset <= a_code.size() &&
			a_expected.size() <= a_code.size() - a_offset &&
			std::equal(a_expected.begin(), a_expected.end(), a_code.begin() + a_offset);
	}

	// The return address classifies the request.
	struct CallerSignature
	{
		BA2Profile::CallerId caller;
		uint32_t preOffset;
		uint32_t callOffset;
		uint32_t returnOffset;
		std::span<const uint8_t> pre;
		std::span<const uint8_t> post;
	};

	namespace CallerBytes
	{
		inline constexpr std::initializer_list<uint8_t> MODERN_PRE{
			0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCE
		};
		inline constexpr std::initializer_list<uint8_t> MODERN_STREAMING_POST{
			0x44, 0x8B, 0x4C, 0x24, 0x68, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCD
		};
		inline constexpr std::initializer_list<uint8_t> MODERN_ARRAY_POST{
			0x45, 0x8B, 0xCE, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCD
		};
		inline constexpr std::initializer_list<uint8_t> OG_PRE{
			0x48, 0x8B, 0xCF, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xF0
		};
		inline constexpr std::initializer_list<uint8_t> OG_STREAMING_POST{
			0x44, 0x8B, 0x4C, 0x24, 0x58, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCD
		};
		inline constexpr std::initializer_list<uint8_t> OG_ARRAY_POST{
			0x45, 0x8B, 0xCE, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCD
		};
	}

	inline constexpr std::array<CallerSignature, 2> MODERN_CALLERS{
		CallerSignature{
			BA2Profile::kCallerStreamingTexture, 0x12C, 0x135, 0x13A,
			CallerBytes::MODERN_PRE, CallerBytes::MODERN_STREAMING_POST },
		CallerSignature{
			BA2Profile::kCallerArraySlice, 0x13C, 0x145, 0x14A,
			CallerBytes::MODERN_PRE, CallerBytes::MODERN_ARRAY_POST }
	};

	inline constexpr std::array<CallerSignature, 2> OG_CALLERS{
		CallerSignature{
			BA2Profile::kCallerStreamingTexture, 0xA0, 0xA9, 0xAE,
			CallerBytes::OG_PRE, CallerBytes::OG_STREAMING_POST },
		CallerSignature{
			BA2Profile::kCallerArraySlice, 0xBB, 0xC4, 0xC9,
			CallerBytes::OG_PRE, CallerBytes::OG_ARRAY_POST }
	};

	struct CallerSiteCheck
	{
		bool preOk{ false };
		bool callOk{ false };
		bool targetOk{ false };
		bool postOk{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return preOk && callOk && targetOk && postOk;
		}
	};

	// Post bytes reload return registers without reading AL.
	[[nodiscard]] inline CallerSiteCheck ValidateCallerSite(
		std::span<const uint8_t> a_code,
		const CallerSignature& a_signature,
		uintptr_t a_root,
		uintptr_t a_seam) noexcept
	{
		CallerSiteCheck check;
		check.preOk = MatchesBytes(a_code, a_signature.preOffset, a_signature.pre);
		check.postOk = MatchesBytes(a_code, a_signature.returnOffset, a_signature.post);

		constexpr size_t callSize = 5;
		if (a_signature.callOffset + callSize > a_code.size() ||
			a_code[a_signature.callOffset] != 0xE8)
			return check;

		check.callOk = true;
		int32_t displacement = 0;
		for (size_t byte = 0; byte < sizeof(displacement); ++byte)
			displacement |= static_cast<int32_t>(
				static_cast<uint32_t>(a_code[a_signature.callOffset + 1 + byte]) << (8 * byte));

		const auto target = a_root + a_signature.callOffset + callSize +
			static_cast<uintptr_t>(static_cast<std::intptr_t>(displacement));
		check.targetOk = target == a_seam;
		return check;
	}

	enum class DelegateReason : uint8_t
	{
		None = 0,
		UnknownCaller,
		AlternateState,
		ForeignDetail,
		DetailMode,
		EngineStream,
		ChunkRange,
		ChunkMetadata,
		Arithmetic
	};

	[[nodiscard]] constexpr std::string_view DelegateReasonName(DelegateReason a_reason) noexcept
	{
		using namespace std::literals;
		switch (a_reason)
		{
		case DelegateReason::None:
			return "none"sv;
		case DelegateReason::UnknownCaller:
			return "unknown-caller"sv;
		case DelegateReason::AlternateState:
			return "alternate-state"sv;
		case DelegateReason::ForeignDetail:
			return "foreign-detail"sv;
		case DelegateReason::DetailMode:
			return "detail-mode"sv;
		case DelegateReason::EngineStream:
			return "engine-stream"sv;
		case DelegateReason::ChunkRange:
			return "chunk-range"sv;
		case DelegateReason::ChunkMetadata:
			return "chunk-metadata"sv;
		case DelegateReason::Arithmetic:
			return "arithmetic"sv;
		default:
			return "unknown"sv;
		}
	}

	struct RequestBounds
	{
		uint16_t first{ 0 };
		uint16_t last{ 0 };
		uint16_t count{ 0 };
		uint16_t descMismatches{ 0 };
		uint32_t inputTotal{ 0 };
		uint32_t outputStart{ 0 };
		uint32_t outputEnd{ 0 };
		uint32_t outputTotal{ 0 };

		[[nodiscard]] constexpr uint16_t ChunkRows() const noexcept
		{
			return static_cast<uint16_t>(last - first + 1);
		}
	};

	struct PreflightResult
	{
		DelegateReason reason{ DelegateReason::None };
		RequestBounds bounds{};
		bool zeroCompressed{ false };
		bool badHeader{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return reason == DelegateReason::None;
		}
	};

	[[nodiscard]] inline bool EngineStreamIsIdle(const Detail& a_detail) noexcept
	{
		const auto& stream = a_detail.stream;
		if (stream.total_in || stream.total_out)
			return false;
		// Never decoded, so no private state to disturb.
		return !stream.state || *ZlibInflate::ModePointer(stream) == ZlibInflate::MODE_HEAD;
	}

	[[nodiscard]] inline bool PointerRangeFits(const void* a_base, uint64_t a_bytes) noexcept
	{
		const auto base = reinterpret_cast<uintptr_t>(a_base);
		return base != 0 &&
			a_bytes <= static_cast<uint64_t>(
				std::numeric_limits<uintptr_t>::max() - base);
	}

	// Proven before any decode or write.
	[[nodiscard]] inline PreflightResult Preflight(
		const Stream* a_stream,
		const std::byte* a_destination,
		uintptr_t a_expectedDetailVtable) noexcept
	{
		PreflightResult result;
		if (!a_stream || !a_destination)
			return { DelegateReason::ChunkMetadata };

		if (!a_stream->resident)
			return { DelegateReason::AlternateState };

		if (!a_stream->detail || !a_stream->chunks || !a_stream->nominalSizes ||
			!a_stream->resident->compressedBase)
			return { DelegateReason::ChunkMetadata };

		if (reinterpret_cast<uintptr_t>(a_stream->detail->vtable) != a_expectedDetailVtable)
			return { DelegateReason::ForeignDetail };

		if (a_stream->detail->directFlag != DETAIL_DIRECT)
			return { DelegateReason::DetailMode };

		if (!EngineStreamIsIdle(*a_stream->detail))
			return { DelegateReason::EngineStream };

		const auto count = a_stream->count;
		const auto first = a_stream->first;
		const auto last = a_stream->last;
		if (!count || count > MAX_CHUNK_COUNT || first > last || last >= count)
			return { DelegateReason::ChunkRange };

		result.bounds.first = static_cast<uint16_t>(first);
		result.bounds.last = static_cast<uint16_t>(last);
		result.bounds.count = static_cast<uint16_t>(count);

		constexpr uint64_t byteLimit{ 0xFFFFFFFFull };
		uint64_t inputTotal = 0;
		uint64_t outputStart = 0;
		uint64_t outputEnd = 0;
		uint64_t outputTotal = 0;
		for (uint32_t chunk = 0; chunk < count; ++chunk)
		{
			const auto& desc = a_stream->chunks[chunk];
			const auto nominal = a_stream->nominalSizes[chunk];

			if (chunk < first)
				outputStart += nominal;
			outputTotal += nominal;
			if (chunk <= last)
				outputEnd += nominal;
			if (outputTotal > byteLimit)
				return { DelegateReason::Arithmetic };

			if (chunk < first || chunk > last)
				continue;

			// Only the selected mips are evidence.
			if (nominal != desc.uncompressedSize)
				++result.bounds.descMismatches;

			if (!desc.compressedSize || !nominal)
			{
				result.reason = DelegateReason::ChunkMetadata;
				result.zeroCompressed = desc.compressedSize == 0;
				return result;
			}

			inputTotal += desc.compressedSize;
			if (inputTotal > byteLimit)
				return { DelegateReason::Arithmetic };
		}

		if (outputStart > outputEnd || outputEnd > outputTotal)
			return { DelegateReason::Arithmetic };

		const auto* base = a_stream->resident->compressedBase;
		if (!PointerRangeFits(base, inputTotal) || !PointerRangeFits(a_destination, outputTotal))
			return { DelegateReason::Arithmetic };

		uint32_t memberStart = 0;
		for (uint32_t chunk = first; chunk <= last; ++chunk)
		{
			const auto compressedSize = a_stream->chunks[chunk].compressedSize;
			if (!ZlibInflate::HasZlibHeader({ base + memberStart, compressedSize }))
			{
				result.reason = DelegateReason::ChunkMetadata;
				result.badHeader = true;
				return result;
			}
			memberStart += compressedSize;
		}

		result.bounds.inputTotal = static_cast<uint32_t>(inputTotal);
		result.bounds.outputStart = static_cast<uint32_t>(outputStart);
		result.bounds.outputEnd = static_cast<uint32_t>(outputEnd);
		result.bounds.outputTotal = static_cast<uint32_t>(outputTotal);
		return result;
	}
}
