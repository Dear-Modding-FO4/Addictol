#pragma once

#include <Zlib/Decoders/AdInflateDecoder.h>

namespace Addictol
{
	struct ZlibApi;
	struct ZlibNgApi;

	template<class Api>
	struct NativeZlibDecoder
	{
		struct State { void* context{}; };
		inline static constexpr InflateCapabilities capabilities{ true, true, true, true, true, true, true, true, true };
		static int32_t Init(State&, int32_t) noexcept;
		static int32_t Inflate(State&, ZlibInflate::Stream&, int32_t) noexcept;
		static int32_t Reset(State&) noexcept;
		static int32_t Reset2(State&, int32_t) noexcept;
		static int32_t ResetKeep(State&) noexcept;
		static int32_t End(State&) noexcept;
		static bool HasPendingOutput(const State&) noexcept;
		static int32_t Copy(State&, const State&) noexcept;
		static int32_t SetDictionary(State&, std::span<const uint8_t>) noexcept;
		static int32_t Sync(State&, ZlibInflate::Stream&) noexcept;
		static int32_t Prime(State&, int32_t, int32_t) noexcept;
		static int32_t Mark(const State&) noexcept;
		static int32_t GetHeader(State&, InflateHeader&) noexcept;
		static int32_t Undermine(State&, int32_t) noexcept;
		static int32_t SyncPoint(const State&) noexcept;
	};

	using ZlibDecoder = NativeZlibDecoder<ZlibApi>;
	using ZlibNgDecoder = NativeZlibDecoder<ZlibNgApi>;
}
