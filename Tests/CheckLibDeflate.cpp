#include "../Addictol/Include/Zlib/AdZlibBackend.h"
#include "Harness.h"

#include <libdeflate/libdeflate.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
	using namespace Addictol;

	struct CompressorDeleter
	{
		void operator()(libdeflate_compressor* a_compressor) const noexcept
		{
			libdeflate_free_compressor(a_compressor);
		}
	};

	struct DecompressorDeleter
	{
		void operator()(libdeflate_decompressor* a_decompressor) const noexcept
		{
			libdeflate_free_decompressor(a_decompressor);
		}
	};

	using CompressorPtr = std::unique_ptr<libdeflate_compressor, CompressorDeleter>;
	using DecompressorPtr = std::unique_ptr<libdeflate_decompressor, DecompressorDeleter>;

	// Texture-like payload: repeated structure with enough entropy to exercise both codec paths.
	std::vector<uint8_t> make_payload(size_t a_size, uint64_t a_seed)
	{
		std::vector<uint8_t> payload(a_size);
		uint64_t state = a_seed | 1;
		for (size_t index = 0; index < a_size; ++index)
		{
			state = state * 6364136223846793005ull + 1442695040888963407ull;
			payload[index] = static_cast<uint8_t>(
				(index % 61 == 0) ? (state >> 33) : (index & 0xFF));
		}
		return payload;
	}

	std::vector<uint8_t> compress(
		libdeflate_compressor* a_compressor,
		const std::vector<uint8_t>& a_payload)
	{
		std::vector<uint8_t> compressed(
			libdeflate_zlib_compress_bound(a_compressor, a_payload.size()));
		const auto written = libdeflate_zlib_compress(
			a_compressor,
			a_payload.data(),
			a_payload.size(),
			compressed.data(),
			compressed.size());
		compressed.resize(written);
		return compressed;
	}

	ZlibExactDecode decode_exact(
		libdeflate_decompressor* a_decompressor,
		std::span<const uint8_t> a_input,
		std::span<uint8_t> a_output)
	{
		ZlibExactDecode decode{};
		decode.codecResult = static_cast<uint32_t>(libdeflate_zlib_decompress_ex(
			a_decompressor,
			a_input.data(),
			a_input.size(),
			a_output.data(),
			a_output.size(),
			&decode.consumed,
			&decode.produced));
		return decode;
	}
}

namespace vmm_tests
{
	void run_libdeflate_checks(Runner& runner)
	{
		runner.test("libdeflate result codes match the mirrored registry", [] {
			require(ZLIB_CODEC_SUCCESS == LIBDEFLATE_SUCCESS, "success code drifted");
			require(ZLIB_CODEC_BAD_DATA == LIBDEFLATE_BAD_DATA, "bad-data code drifted");
			require(ZLIB_CODEC_SHORT_OUTPUT == LIBDEFLATE_SHORT_OUTPUT, "short-output code drifted");
			require(
				ZLIB_CODEC_INSUFFICIENT_SPACE == LIBDEFLATE_INSUFFICIENT_SPACE,
				"insufficient-space code drifted");
		});

		runner.test("libdeflate decodes mip-sized members exactly", [&runner] {
			CompressorPtr compressor{ libdeflate_alloc_compressor(6) };
			DecompressorPtr decompressor{ libdeflate_alloc_decompressor() };
			require(compressor && decompressor, "the codec could not be allocated");

			constexpr std::array sizes{
				1ull * 1024 * 1024,
				2ull * 1024 * 1024,
				4ull * 1024 * 1024,
				16ull * 1024 * 1024
			};

			for (const auto size : sizes)
			{
				const auto payload = make_payload(static_cast<size_t>(size), size);
				const auto compressed = compress(compressor.get(), payload);
				std::vector<uint8_t> output(payload.size() + 4096, 0);

				const auto decode = decode_exact(decompressor.get(), compressed, output);
				require(
					ClassifyExactDecode(decode, compressed.size(), payload.size()) ==
						ZlibExactStatus::Success,
					"an exact member was not classified as a success");
				require(decode.consumed == compressed.size(), "the member was not fully consumed");
				require(decode.produced == payload.size(), "the member did not produce fullSize bytes");
				require(
					std::equal(payload.begin(), payload.end(), output.begin()),
					"the decoded bytes differ from the payload");
				require(
					output[payload.size()] == 0,
					"the codec wrote past the member's uncompressed size");

				runner.info(std::string("libdeflate exact decode ")
					.append(std::to_string(size / (1024 * 1024)))
					.append(" MiB: ")
					.append(std::to_string(compressed.size()))
					.append(" compressed bytes"));
			}
		});

		runner.test("libdeflate failures classify into distinct fallback reasons", [] {
			CompressorPtr compressor{ libdeflate_alloc_compressor(6) };
			DecompressorPtr decompressor{ libdeflate_alloc_decompressor() };
			require(compressor && decompressor, "the codec could not be allocated");

			const auto payload = make_payload(256 * 1024, 7);
			const auto compressed = compress(compressor.get(), payload);
			std::vector<uint8_t> output(payload.size(), 0);

			std::vector<uint8_t> tooSmall(payload.size() / 2, 0);
			const auto capacity = decode_exact(decompressor.get(), compressed, tooSmall);
			require(
				ClassifyExactDecode(capacity, compressed.size(), payload.size()) ==
					ZlibExactStatus::Capacity,
				"an undersized output buffer was not a capacity failure");

			auto corrupt = compressed;
			corrupt[corrupt.size() / 2] ^= 0xFF;
			const auto decode = decode_exact(decompressor.get(), corrupt, output);
			require(
				ClassifyExactDecode(decode, corrupt.size(), payload.size()) == ZlibExactStatus::Decode ||
					ClassifyExactDecode(decode, corrupt.size(), payload.size()) ==
						ZlibExactStatus::SizeMismatch,
				"a corrupted member was accepted as an exact decode");

			const auto exact = decode_exact(decompressor.get(), compressed, output);
			require(
				ClassifyExactDecode(exact, compressed.size(), payload.size() + 1) ==
					ZlibExactStatus::SizeMismatch,
				"a wrong expected size was accepted");
			require(
				ClassifyExactDecode(exact, compressed.size() + 1, payload.size()) ==
					ZlibExactStatus::SizeMismatch,
				"a wrong expected member length was accepted");
			require(
				ClassifyExactDecode(exact, compressed.size(), payload.size()) ==
					ZlibExactStatus::Success,
				"the control decode was not exact");
		});
	}
}
