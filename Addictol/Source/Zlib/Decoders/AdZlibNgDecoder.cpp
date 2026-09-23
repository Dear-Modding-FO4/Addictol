#include <Zlib/Decoders/AdNativeZlibDecoder.h>
#include <Zlib/Decoders/AdInflateStreamMirror.h>
#include <zlib-ng.h>

namespace Addictol
{
	struct ZlibNgApi
	{
		using Stream = zng_stream;
		using Header = zng_gz_header;
		inline static constexpr auto version = ZLIBNG_VERSION;
		static int32_t Init(Stream* a_stream, int32_t a_bits, const char*, size_t) noexcept
		{
			return zng_inflateInit2(a_stream, a_bits);
		}
		inline static constexpr auto init = Init;
		inline static constexpr auto inflate = zng_inflate;
		inline static constexpr auto reset = zng_inflateReset;
		inline static constexpr auto reset2 = zng_inflateReset2;
		inline static constexpr auto resetKeep = zng_inflateResetKeep;
		inline static constexpr auto end = zng_inflateEnd;
		inline static constexpr auto copy = zng_inflateCopy;
		inline static constexpr auto setDictionary = zng_inflateSetDictionary;
		inline static constexpr auto sync = zng_inflateSync;
		inline static constexpr auto prime = zng_inflatePrime;
		inline static constexpr auto mark = zng_inflateMark;
		inline static constexpr auto getHeader = zng_inflateGetHeader;
		inline static constexpr auto undermine = zng_inflateUndermine;
		inline static constexpr auto syncPoint = zng_inflateSyncPoint;
	};
}

#include "AdNativeZlibDecoder.inl"
template struct Addictol::NativeZlibDecoder<Addictol::ZlibNgApi>;
