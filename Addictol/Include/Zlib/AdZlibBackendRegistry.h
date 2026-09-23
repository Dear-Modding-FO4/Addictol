#pragma once
#include <Zlib/AdOwnedInflate.h>
#include <Zlib/Decoders/AdNativeZlibDecoder.h>
#include <Zlib/Decoders/AdIsalDecoder.h>

namespace Addictol
{
	template<ZlibBackendKind Kind, class WholeType, class StreamingType>
	struct ZlibBackendRow
	{
		inline static constexpr auto kind = Kind;
		using Whole = WholeType;
		using Streaming = StreamingType;
	};
	template<class Function>
	decltype(auto) VisitSelectedZlibBackend(Function&& a_function)
	{
		switch (GetSelectedZlibBackendKind())
		{
#define ZLIB_VISIT(Kind, Name, Whole, Streaming) \
		case ZlibBackendKind::Kind: return a_function.template operator()<ZlibBackendRow<ZlibBackendKind::Kind, Whole, Streaming>>();
			ADDICTOL_ZLIB_BACKENDS(ZLIB_VISIT)
#undef ZLIB_VISIT
		default:
			return a_function.template operator()<ZlibBackendRow<DEFAULT_ZLIB_BACKEND, LibDeflateZlibBackend, ZlibNgDecoder>>();
		}
	}
}
