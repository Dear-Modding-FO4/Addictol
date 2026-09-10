#include "../Addictol/Include/Zlib/AdZlibBackend.h"
#include "Harness.h"

#include <libdeflate/libdeflate.h>

#include <algorithm>
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

	struct State
	{
		uint32_t mode{ ZlibInflate::MODE_HEAD };
		uint32_t last{ 0 };
	};

	using CompressorPtr = std::unique_ptr<libdeflate_compressor, CompressorDeleter>;

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
		vmm_tests::require(written != 0, "fixture compression failed");
		compressed.resize(written);
		return compressed;
	}

	ZlibInflate::Stream make_stream(
		State& a_state,
		std::span<const uint8_t> a_input,
		std::span<uint8_t> a_output)
	{
		ZlibInflate::Stream stream{};
		stream.next_in = a_input.data();
		stream.avail_in = static_cast<uint32_t>(a_input.size());
		stream.next_out = a_output.data();
		stream.avail_out = static_cast<uint32_t>(a_output.size());
		stream.state = &a_state;
		stream.msg = "stale";
		stream.data_type = 2;
		return stream;
	}
}

namespace vmm_tests
{
	void run_libdeflate_checks(Runner& runner)
	{
		runner.test("libdeflate serves mip-sized members through the production backend", [&runner] {
			CompressorPtr compressor{ libdeflate_alloc_compressor(6) };
			require(compressor != nullptr, "the fixture compressor could not be allocated");

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
				State state;
				auto stream = make_stream(state, compressed, output);
				size_t stockCalls{ 0 };

				const auto outcome = ServeZlib<LibDeflateZlibBackend>(
					&stream,
					2,
					[&](ZlibInflate::Stream*, int32_t) noexcept {
						++stockCalls;
						return -2;
					},
					false,
					0,
					[]() noexcept { return uint64_t{ 0 }; });

				require(stockCalls == 0, "valid compressed member fell back to stock");
				require(
					outcome.primaryAttempted &&
						outcome.primaryBackendId == ZlibBackendRegistryId(ZlibBackendKind::LibDeflate) &&
						outcome.servedBackendId == outcome.primaryBackendId &&
						outcome.fallbackBackendId == 0 && outcome.fallbackReasonId == 0,
					"production codec success reported the wrong serving path");
				require(outcome.zlibResult == ZlibInflate::Z_STREAM_END,
					"production codec did not report stream completion");
				require(outcome.consumed == compressed.size(), "the member was not fully consumed");
				require(outcome.produced == payload.size(), "the member did not produce fullSize bytes");
				require(
					std::equal(payload.begin(), payload.end(), output.begin()),
					"the decoded bytes differ from the payload");
				require(
					output[payload.size()] == 0,
					"the codec wrote past the member's uncompressed size");
				require(
					stream.next_in == compressed.data() + compressed.size() &&
						stream.avail_in == 0 && stream.total_in == compressed.size(),
					"production codec consumption was not committed to the stream");
				require(
					stream.next_out == output.data() + payload.size() &&
						stream.avail_out == 4096 && stream.total_out == payload.size(),
					"production codec completion did not preserve spare output");
				require(
					state.mode == ZlibInflate::MODE_DONE && state.last == 1 &&
						stream.msg == nullptr && stream.data_type == ZlibInflate::DATA_TYPE_DONE,
					"production codec did not commit terminal stream state");
				require(
					stream.adler == libdeflate_adler32(1, payload.data(), payload.size()),
					"production codec completion published the wrong checksum");

				runner.info(std::string("libdeflate exact decode ")
					.append(std::to_string(size / (1024 * 1024)))
					.append(" MiB: ")
					.append(std::to_string(compressed.size()))
					.append(" compressed bytes"));
			}
		});

		for (const bool corrupt : { false, true })
		{
			runner.test(corrupt ?
				"libdeflate checksum failure delegates to stock" :
				"libdeflate insufficient output delegates to stock", [corrupt] {
				CompressorPtr compressor{ libdeflate_alloc_compressor(6) };
				require(compressor != nullptr, "the fixture compressor could not be allocated");
				const auto payload = make_payload(256 * 1024, 7);
				auto compressed = compress(compressor.get(), payload);
				if (corrupt)
					compressed.back() ^= 1;

				const auto capacity = corrupt ? payload.size() : payload.size() / 2;
				std::vector<uint8_t> output(capacity + 16, 0xA5);
				State state;
				auto stream = make_stream(state, compressed, std::span{ output }.first(capacity));
				const auto before = stream;
				size_t stockCalls{ 0 };
				bool forwarded{ false };
				bool unchanged{ false };
				constexpr int32_t stockResult{ -5 };

				const auto outcome = ServeZlib<LibDeflateZlibBackend>(
					&stream,
					2,
					[&](ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept {
						++stockCalls;
						forwarded = a_stream == &stream && a_flush == 2;
						unchanged =
							stream.next_in == before.next_in && stream.avail_in == before.avail_in &&
							stream.total_in == before.total_in && stream.next_out == before.next_out &&
							stream.avail_out == before.avail_out && stream.total_out == before.total_out &&
							stream.state == before.state && stream.msg == before.msg &&
							stream.adler == before.adler && stream.data_type == before.data_type &&
							state.mode == ZlibInflate::MODE_HEAD && state.last == 0;
						if (forwarded)
						{
							a_stream->next_in += 3;
							a_stream->avail_in -= 3;
							a_stream->total_in += 3;
							a_stream->next_out += 5;
							a_stream->avail_out -= 5;
							a_stream->total_out += 5;
						}
						return stockResult;
					},
					false,
					0,
					[]() noexcept { return uint64_t{ 0 }; });

				require(stockCalls == 1 && forwarded,
					"production codec failure did not forward the stream and flush exactly once");
				require(unchanged, "production codec failure changed stream state before stock fallback");
				require(
					outcome.primaryAttempted &&
						outcome.primaryBackendId == ZlibBackendRegistryId(ZlibBackendKind::LibDeflate) &&
						outcome.fallbackBackendId == ZlibBackendRegistryId(ZlibBackendKind::Stock) &&
						outcome.servedBackendId == outcome.fallbackBackendId &&
						outcome.fallbackReasonId == ZlibFallbackReasonRegistryId(ZlibFallbackReason::Decode),
					"production codec failure reported the wrong fallback path");
				require(outcome.zlibResult == stockResult,
					"production codec fallback did not propagate the stock result");
				require(outcome.consumed == 3 && outcome.produced == 5,
					"production codec fallback did not report the stock byte deltas");
				require(
					std::all_of(output.begin() + capacity, output.end(),
						[](uint8_t a_byte) { return a_byte == 0xA5; }),
					"production codec wrote beyond the output capacity");
			});
		}
	}
}
