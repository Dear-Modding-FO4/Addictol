#pragma once

#include <Zlib/AdZlibInflate.h>
#include <span>
#include <vector>

namespace vmm_tests
{
	std::vector<uint8_t> compress_zlib_fixture(std::span<const uint8_t> a_input, int32_t a_bits,
		std::span<const uint8_t> a_dictionary = {}, bool a_fullFlush = false);

	class ZlibOracle
	{
	public:
		explicit ZlibOracle(int32_t a_bits);
		~ZlibOracle();
		ZlibOracle(const ZlibOracle&) = delete;
		ZlibOracle& operator=(const ZlibOracle&) = delete;
		int32_t Inflate(int32_t a_flush);
		int32_t Sync();
		int32_t Reset();
		int32_t SetDictionary(std::span<const uint8_t> a_dictionary);
		Addictol::ZlibInflate::Stream stream{};

	private:
		int32_t Call(int32_t a_flush, bool a_sync);
		void* m_native{};
	};
}
