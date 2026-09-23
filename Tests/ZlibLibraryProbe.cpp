#include "ZlibLibraryProbe.h"

// Keep zlib's compatibility macros out of the zlib-ng translation unit.
#define Z_PREFIX
#include <zlib/zlib.h>

namespace vmm_tests
{
	bool decode_prefixed_zlib(std::span<uint8_t> a_input, std::span<uint8_t> a_output)
	{
		z_stream stream{};
		if (z_inflateInit(&stream) != Z_OK)
			return false;
		stream.next_in = a_input.data();
		stream.avail_in = static_cast<uint32_t>(a_input.size());
		stream.next_out = a_output.data();
		stream.avail_out = static_cast<uint32_t>(a_output.size());
		const auto result = z_inflate(&stream, Z_FINISH);
		const auto end = z_inflateEnd(&stream);
		return result == Z_STREAM_END && end == Z_OK &&
			stream.total_out == a_output.size() && stream.avail_in == 0;
	}
}
