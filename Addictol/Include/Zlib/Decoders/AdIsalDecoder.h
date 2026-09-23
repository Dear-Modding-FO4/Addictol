#pragma once

#include <Zlib/Decoders/AdInflateDecoder.h>

namespace Addictol
{
	struct IsalDecoder
	{
		struct State { void* context{}; };
		inline static constexpr InflateCapabilities capabilities{ true, true };
		static int32_t Init(State&, int32_t) noexcept;
		static int32_t Inflate(State&, ZlibInflate::Stream&, int32_t) noexcept;
		static int32_t Reset(State&) noexcept;
		static int32_t Reset2(State&, int32_t) noexcept;
		static int32_t End(State&) noexcept;
		static bool HasPendingOutput(const State&) noexcept;
		static int32_t Copy(State&, const State&) noexcept;
		static int32_t SetDictionary(State&, std::span<const uint8_t>) noexcept;
	};
}
