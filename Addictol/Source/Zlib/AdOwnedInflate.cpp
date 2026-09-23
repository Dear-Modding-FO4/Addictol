#include <Zlib/AdOwnedInflate.h>
#include <Zlib/Decoders/AdNativeZlibDecoder.h>
#include <Zlib/Decoders/AdIsalDecoder.h>

namespace Addictol
{
	static_assert(WholeInflateDecoder<LibDeflateZlibBackend>);
	static_assert(StreamingInflateDecoder<ZlibDecoder>);
	static_assert(StreamingInflateDecoder<ZlibNgDecoder>);
	static_assert(StreamingInflateDecoder<IsalDecoder>);

	template struct OwnedInflate<NoWholeInflateDecoder, ZlibDecoder>;
	template struct OwnedInflate<NoWholeInflateDecoder, ZlibNgDecoder>;
	template struct OwnedInflate<NoWholeInflateDecoder, IsalDecoder>;
	template struct OwnedInflate<LibDeflateZlibBackend, ZlibNgDecoder>;
	template struct OwnedInflate<LibDeflateZlibBackend, IsalDecoder>;
}
