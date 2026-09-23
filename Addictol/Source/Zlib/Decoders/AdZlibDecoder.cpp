#include <Zlib/Decoders/AdNativeZlibDecoder.h>
#include <Zlib/Decoders/AdInflateStreamMirror.h>

#define Z_PREFIX
#include <zlib/zlib.h>

namespace Addictol
{
	struct ZlibApi
	{
		using Stream = z_stream;
		using Header = z_gz_header;
		inline static constexpr auto version = ZLIB_VERSION;
		inline static constexpr auto init = z_inflateInit2_;
		inline static constexpr auto inflate = z_inflate;
		inline static constexpr auto reset = z_inflateReset;
		inline static constexpr auto reset2 = z_inflateReset2;
		inline static constexpr auto resetKeep = z_inflateResetKeep;
		inline static constexpr auto end = z_inflateEnd;
		inline static constexpr auto copy = z_inflateCopy;
		inline static constexpr auto setDictionary = z_inflateSetDictionary;
		inline static constexpr auto sync = z_inflateSync;
		inline static constexpr auto prime = z_inflatePrime;
		inline static constexpr auto mark = z_inflateMark;
		inline static constexpr auto getHeader = z_inflateGetHeader;
		inline static constexpr auto undermine = z_inflateUndermine;
		inline static constexpr auto syncPoint = z_inflateSyncPoint;
	};
}

#include "AdNativeZlibDecoder.inl"
template struct Addictol::NativeZlibDecoder<Addictol::ZlibApi>;
