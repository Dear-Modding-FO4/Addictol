#include "ZlibOracle.h"
#include "Harness.h"

#define Z_PREFIX
#include <zlib/zlib.h>

namespace vmm_tests
{
	std::vector<uint8_t> compress_zlib_fixture(std::span<const uint8_t> a_input, int32_t a_bits,
		std::span<const uint8_t> a_dictionary, bool a_fullFlush)
	{
		z_stream stream{};
		require(z_deflateInit2(&stream, 6, Z_DEFLATED, a_bits, 8, Z_DEFAULT_STRATEGY) == Z_OK, "oracle deflate init");
		if (!a_dictionary.empty())
			require(z_deflateSetDictionary(&stream, a_dictionary.data(), static_cast<uint32_t>(a_dictionary.size())) == Z_OK, "oracle dictionary");
		std::vector<uint8_t> output(z_deflateBound(&stream, static_cast<uint32_t>(a_input.size())) + 64);
		stream.next_in = const_cast<uint8_t*>(a_input.data());
		stream.avail_in = static_cast<uint32_t>(a_input.size());
		stream.next_out = output.data();
		stream.avail_out = static_cast<uint32_t>(output.size());
		if (a_fullFlush)
		{
			stream.avail_in = static_cast<uint32_t>(a_input.size() / 2);
			require(z_deflate(&stream, Z_FULL_FLUSH) == Z_OK && stream.avail_in == 0, "oracle full flush");
			stream.avail_in = static_cast<uint32_t>(a_input.size() - a_input.size() / 2);
		}
		const auto result = z_deflate(&stream, Z_FINISH);
		output.resize(stream.total_out);
		z_deflateEnd(&stream);
		require(result == Z_STREAM_END, "oracle fixture compression");
		return output;
	}

	ZlibOracle::ZlibOracle(int32_t a_bits)
	{
		auto* native = new z_stream{};
		const auto result = z_inflateInit2(native, a_bits);
		if (result != Z_OK)
		{
			delete native;
			throw Failure("oracle inflate init");
		}
		m_native = native;
	}

	ZlibOracle::~ZlibOracle()
	{
		auto* native = static_cast<z_stream*>(m_native);
		z_inflateEnd(native);
		delete native;
	}

	int32_t ZlibOracle::Inflate(int32_t a_flush)
	{
		return Call(a_flush, false);
	}

	int32_t ZlibOracle::Sync()
	{
		return Call(0, true);
	}

	int32_t ZlibOracle::Call(int32_t a_flush, bool a_sync)
	{
		auto& native = *static_cast<z_stream*>(m_native);
		native.next_in = const_cast<uint8_t*>(stream.next_in);
		native.avail_in = stream.avail_in;
		native.next_out = stream.next_out;
		native.avail_out = stream.avail_out;
		const auto result = a_sync ? z_inflateSync(&native) : z_inflate(&native, a_flush);
		stream.next_in = native.next_in;
		stream.avail_in = native.avail_in;
		stream.total_in = native.total_in;
		stream.next_out = native.next_out;
		stream.avail_out = native.avail_out;
		stream.total_out = native.total_out;
		stream.msg = native.msg;
		stream.adler = native.adler;
		stream.data_type = native.data_type;
		return result;
	}

	int32_t ZlibOracle::Reset()
	{
		stream.total_in = stream.total_out = 0;
		return z_inflateReset(static_cast<z_stream*>(m_native));
	}

	int32_t ZlibOracle::SetDictionary(std::span<const uint8_t> a_dictionary)
	{
		return z_inflateSetDictionary(static_cast<z_stream*>(m_native), a_dictionary.data(), static_cast<uint32_t>(a_dictionary.size()));
	}
}
