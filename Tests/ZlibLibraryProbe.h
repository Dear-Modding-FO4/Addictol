#pragma once

#include <cstdint>
#include <span>

namespace vmm_tests
{
	bool decode_prefixed_zlib(std::span<uint8_t> a_input, std::span<uint8_t> a_output);
}
