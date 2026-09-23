#include "Harness.h"
#include "ZlibLibraryProbe.h"

#include <igzip_lib.h>
#include <zlib-ng.h>

#include <memory>

namespace vmm_tests
{
	void run_zlib_library_checks(Runner& runner)
	{
		runner.test("zlib, zlib-ng and ISA-L decode a zlib stream", [] {
			constexpr std::string_view expected = "owned zlib linkage";
			uint8_t compressed[]{
				0x78, 0x9C, 0xCB, 0x2F, 0xCF, 0x4B, 0x4D, 0x51, 0xA8, 0xCA, 0xC9, 0x4C, 0x52,
				0xC8, 0xC9, 0xCC, 0xCB, 0x4E, 0x4C, 0x4F, 0x05, 0x00, 0x42, 0x24, 0x06, 0xEA
			};
			std::array<uint8_t, expected.size()> zlibOutput{}, ngOutput{}, isalOutput{};
			require(decode_prefixed_zlib(compressed, zlibOutput), "prefixed zlib decode failed");

			zng_stream ng{};
			require(zng_inflateInit(&ng) == Z_OK, "zlib-ng init failed");
			ng.next_in = compressed;
			ng.avail_in = sizeof(compressed);
			ng.next_out = ngOutput.data();
			ng.avail_out = static_cast<uint32_t>(ngOutput.size());
			const auto ngResult = zng_inflate(&ng, Z_FINISH);
			const auto ngEnd = zng_inflateEnd(&ng);
			require(ngResult == Z_STREAM_END && ngEnd == Z_OK &&
				ng.total_out == expected.size() && ng.avail_in == 0, "zlib-ng decode failed");

			auto isal = std::make_unique<inflate_state>();
			isal_inflate_init(isal.get());
			isal->crc_flag = ISAL_ZLIB;
			isal->next_in = compressed;
			isal->avail_in = sizeof(compressed);
			isal->next_out = isalOutput.data();
			isal->avail_out = static_cast<uint32_t>(isalOutput.size());
			require(isal_inflate(isal.get()) == ISAL_DECOMP_OK &&
				isal->block_state == ISAL_BLOCK_FINISH &&
				isal->total_out == expected.size() && isal->avail_in == 0, "ISA-L decode failed");
			require(zlibOutput == ngOutput && ngOutput == isalOutput &&
				std::string_view(reinterpret_cast<const char*>(ngOutput.data()), ngOutput.size()) == expected,
				"decoded bytes differ");
		});
	}
}
