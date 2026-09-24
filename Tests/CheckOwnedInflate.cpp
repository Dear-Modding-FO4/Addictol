#include "Harness.h"
#include "ZlibOracle.h"
#include <Zlib/AdOwnedInflate.h>
#include <Zlib/Decoders/AdNativeZlibDecoder.h>
#include <Zlib/Decoders/AdIsalDecoder.h>
#include <algorithm>
#include <thread>

namespace
{
	using namespace Addictol;
	using Stream = ZlibInflate::Stream;
	using vmm_tests::require;

	struct ObservedWholeDecoder
	{
		inline static size_t calls{};
		inline static uint8_t* output{};
		static ZlibDecodeResult Decode(std::span<const uint8_t> a_input, std::span<uint8_t> a_output) noexcept
		{
			++calls;
			output = a_output.data();
			return LibDeflateZlibBackend::Decode(a_input, a_output);
		}
	};

	struct Engine
	{
		const char* name;
		int32_t (*init)(Stream*, int32_t);
		int32_t (*inflate)(Stream*, int32_t);
		int32_t (*reset)(Stream*);
		int32_t (*reset2)(Stream*, int32_t);
		int32_t (*end)(Stream*);
		int32_t (*copy)(Stream*, const Stream*);
		int32_t (*dictionary)(Stream*, std::span<const uint8_t>);
		bool (*owned)(const Stream*);
		int32_t (*mark)(Stream*);
		int32_t (*resetKeep)(Stream*);
		int32_t (*header)(Stream*, InflateHeader*);
		int32_t (*sync)(Stream*);
		bool controls;
	};

	template<class Whole, class Decoder>
	constexpr Engine MakeEngine(const char* a_name)
	{
		using Owned = OwnedInflate<Whole, Decoder>;
		return { a_name, [](Stream* a_stream, int32_t a_bits) { return Owned::Init2(a_stream, a_bits); },
			Owned::Inflate, Owned::Reset, Owned::Reset2, Owned::End, Owned::Copy, Owned::SetDictionary, Owned::IsOwned,
			Owned::Mark, Owned::ResetKeep, Owned::GetHeader, Owned::Sync, Decoder::capabilities.mark };
	}

	constexpr std::array s_engines{
		MakeEngine<NoWholeInflateDecoder, ZlibDecoder>("zlib"),
		MakeEngine<NoWholeInflateDecoder, ZlibNgDecoder>("zlib-ng"),
		MakeEngine<NoWholeInflateDecoder, IsalDecoder>("isa-l"),
		MakeEngine<ObservedWholeDecoder, ZlibNgDecoder>("hybrid-zlib-ng"),
		MakeEngine<ObservedWholeDecoder, IsalDecoder>("hybrid-isa-l")
	};

	struct OwnedStream
	{
		const Engine& engine;
		Stream stream{};
		~OwnedStream() { if (engine.owned(&stream)) engine.end(&stream); }
	};

	std::vector<uint8_t> Payload(size_t a_size, bool a_repeated = false)
	{
		std::vector<uint8_t> bytes(a_size);
		uint32_t rng = 192837;
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
			bytes[i] = a_repeated ? 'x' : i % 17 == 0 ? static_cast<uint8_t>(rng) : static_cast<uint8_t>(i % 251);
		}
		return bytes;
	}

	void SameEnd(const Stream& a_actual, const Stream& a_reference, const uint8_t* a_actualOutput,
		const uint8_t* a_referenceOutput, const std::string& a_context)
	{
		require(a_actual.total_in == a_reference.total_in, a_context + " final total_in");
		require(a_actual.total_out == a_reference.total_out, a_context + " final total_out");
		require(a_actual.next_in == a_reference.next_in && a_actual.avail_in == a_reference.avail_in, a_context + " trailer position");
		require(a_actual.next_out - a_actualOutput == a_reference.next_out - a_referenceOutput &&
			a_actual.avail_out == a_reference.avail_out, a_context + " final output position");
		require(a_actual.adler == a_reference.adler, a_context + " final checksum");
		require(a_actual.data_type == a_reference.data_type, a_context + " final data_type");
	}

	struct Pattern
	{
		const char* name;
		int32_t bits{ 15 }, flush{};
		uint32_t outputChunk{ UINT32_MAX }, inputChunk{ UINT32_MAX };
		bool reset{}, peek{}, corrupt{}, repeated{};
		size_t size{ 2 * 1024 * 1024 };
		bool skipWhole{};
	};

	void RunPattern(const Engine& a_engine, const Pattern& a_pattern, size_t a_size)
	{
		const auto payload = Payload(a_size, a_pattern.repeated);
		const auto compressionBits = a_pattern.bits >= 32 ? 31 : a_pattern.bits;
		auto compressed = vmm_tests::compress_zlib_fixture(payload, compressionBits);
		const auto streamSize = compressed.size();
		if (a_pattern.corrupt)
			compressed[compressed.size() - (compressionBits >= 16 ? 8 : 1)] ^= 0x80;
		if (!a_pattern.repeated)
			compressed.insert(compressed.end(), { 0xEA, 0x7B });
		OwnedStream owned{ a_engine };
		require(a_engine.init(&owned.stream, a_pattern.bits) == INFLATE_OK, std::string(a_engine.name) + " init");
		vmm_tests::ZlibOracle oracle(a_pattern.bits);
		for (int reuse = 0; reuse < (a_pattern.reset ? 2 : 1); ++reuse)
		{
			const auto wholeCalls = ObservedWholeDecoder::calls;
			std::vector<uint8_t> actual(payload.size() + 16, 0xCC), reference(actual);
			auto& stream = owned.stream;
			auto& expected = oracle.stream;
			stream.next_in = expected.next_in = compressed.data();
			const uint32_t firstInput = std::min<uint32_t>(static_cast<uint32_t>(compressed.size()), a_pattern.inputChunk);
			stream.avail_in = expected.avail_in = firstInput;
			uint32_t provided = firstInput, referenceProvided = firstInput;
			bool previousEmpty = false;
			int32_t result = INFLATE_OK;
			size_t calls = 0;
			while (result != INFLATE_END && result != INFLATE_DATA_ERROR)
			{
				require(++calls < 4096, "inflate stalled");
				const std::string context = std::string(a_engine.name) + "/" + a_pattern.name + "/call=" + std::to_string(calls);
				const auto before = stream.total_out;
				bool refilled = false;
				if (stream.avail_in == 0 && provided < compressed.size())
				{
					stream.next_in = compressed.data() + provided;
					stream.avail_in = std::min<uint32_t>(static_cast<uint32_t>(compressed.size()) - provided, a_pattern.inputChunk);
					provided += stream.avail_in;
					refilled = true;
				}
				if (expected.avail_in == 0 && referenceProvided < compressed.size())
				{
					expected.next_in = compressed.data() + referenceProvided;
					expected.avail_in = std::min<uint32_t>(static_cast<uint32_t>(compressed.size()) - referenceProvided, a_pattern.inputChunk);
					referenceProvided += expected.avail_in;
				}
				const auto outputCount = std::min<uint32_t>(static_cast<uint32_t>(payload.size()) - stream.total_out, a_pattern.outputChunk);
				const auto referenceCount = std::min<uint32_t>(static_cast<uint32_t>(payload.size()) - expected.total_out, a_pattern.outputChunk);
				stream.next_out = actual.data() + stream.total_out;
				stream.avail_out = outputCount;
				expected.next_out = reference.data() + expected.total_out;
				expected.avail_out = referenceCount;
				const auto referenceResult = oracle.Inflate(a_pattern.flush);
				result = a_engine.inflate(&stream, a_pattern.flush);
				if (calls == 1)
				{
					if (ObservedWholeDecoder::calls != wholeCalls)
						require(ObservedWholeDecoder::calls == wholeCalls + 1 && ObservedWholeDecoder::output == actual.data(),
							context + " whole decode was retried or used a private output buffer");
					if (a_pattern.skipWhole)
					{
						require(outputCount < firstInput, context + " skip fixture does not exceed the output window");
						require(ObservedWholeDecoder::calls == wholeCalls, context + " whole decoder was not skipped");
						const auto* state = ZlibOwnedState::Find(&stream);
						require(state->outcomePolicy == ZlibOwnedPolicy::Streaming &&
							(state->fallbackReason == ZlibFallbackReason::Capacity || state->fallbackReason == ZlibFallbackReason::NoWhole),
							context + " wrong window-limited policy");
					}
				}
				require(result == referenceResult, context + " return code actual=" + std::to_string(result) + " oracle=" + std::to_string(referenceResult));
				require(std::equal(actual.begin(), actual.begin() + stream.total_out, payload.begin()), context + " output bytes");
				require(!(previousEmpty && !refilled && stream.total_out > before), context + " output arrived after empty input accounting");
				if (provided >= streamSize && stream.total_out < payload.size() && !a_pattern.corrupt)
					require(stream.avail_in != 0, context + " hidden pending output");
				previousEmpty = stream.avail_in == 0;
				if (a_pattern.peek)
					break;
				if (result == INFLATE_END)
					SameEnd(stream, expected, actual.data(), reference.data(), context);
			}
			if (a_pattern.corrupt)
				require(result == INFLATE_DATA_ERROR, "corrupt trailer accepted");
			if (!a_pattern.peek && !a_pattern.corrupt)
				require(stream.total_out == payload.size(), "incomplete output");
			require(ObservedWholeDecoder::calls <= wholeCalls + 1, "whole decode retried after streaming selection");
			require(std::all_of(actual.begin() + payload.size(), actual.end(), [](uint8_t b) { return b == 0xCC; }), "output guard changed");
			if (a_pattern.reset)
				require(a_engine.reset(&stream) == oracle.Reset(), "reset result");
		}
	}
}

namespace vmm_tests
{
	void run_owned_inflate_checks(Runner& runner)
	{
		constexpr std::array patterns{
			Pattern{ "one-shot finish", 15, 4 },
			Pattern{ .name = "166 KiB windows", .outputChunk = 166 * 1024, .skipWhole = true },
			Pattern{ "finish with limited output", 15, 4, 166 * 1024, UINT32_MAX, false, false, false, true },
			Pattern{ .name = "258 KiB windows", .outputChunk = 258 * 1024, .skipWhole = true },
			Pattern{ "64 KiB input refills", 15, 0, UINT32_MAX, 64 * 1024 },
			Pattern{ "reset and reuse", 15, 4, UINT32_MAX, UINT32_MAX, true },
			Pattern{ "sync-flush save peek", 15, 2, 2, UINT32_MAX, false, true },
			Pattern{ "raw windows", -15, 0, 166 * 1024 },
			Pattern{ "gzip windows", 31, 0, 166 * 1024 },
			Pattern{ "auto gzip windows", 47, 0, 166 * 1024 },
			Pattern{ "corrupt trailer fallback", 15, 4, UINT32_MAX, UINT32_MAX, false, false, true },
			Pattern{ "corrupt gzip checksum", 31, 4, UINT32_MAX, UINT32_MAX, false, false, true },
			Pattern{ "pending output accounting", -15, 0, 2 * 1024 * 1024 - 1, UINT32_MAX, false, false, false, true },
			Pattern{ "split wrapper headers", 47, 0, UINT32_MAX, 1, false, false, false, false, 1024 }
		};
		for (const auto& pattern : patterns)
		{
			runner.test(pattern.name, [&] {
				for (const auto& engine : s_engines)
					RunPattern(engine, pattern, pattern.size);
			});
		}

		runner.test("owned small streams and copy isolation", [] {
			for (const auto& engine : s_engines)
			{
				RunPattern(engine, Pattern{ "small finish", 15, 4 }, 1024);
				const auto payload = Payload(4096);
				auto input = compress_zlib_fixture(payload, 15);
				OwnedStream source{ engine }, copy{ engine };
				uint64_t destinationSeed{};
				copy.stream.state = &destinationSeed;
				require(engine.init(&source.stream, 15) == INFLATE_OK, "copy init");
				std::array<uint8_t, 1> first{};
				source.stream.next_in = input.data();
				source.stream.avail_in = static_cast<uint32_t>(input.size());
				source.stream.next_out = first.data();
				source.stream.avail_out = 1;
				require(engine.inflate(&source.stream, 0) == INFLATE_OK, "copy prefix");
				require(engine.copy(&copy.stream, &source.stream) == INFLATE_OK, std::string(engine.name) + " copy");
				require(engine.end(&source.stream) == INFLATE_OK, "source end");
				std::vector<uint8_t> output(payload.size() - 1);
				copy.stream.next_out = output.data();
				copy.stream.avail_out = static_cast<uint32_t>(output.size());
				require(engine.inflate(&copy.stream, 4) == INFLATE_END, "copy finish after source end");
				require(std::equal(output.begin(), output.end(), payload.begin() + 1), "copy bytes");
				int32_t endResult{};
				std::thread release([&] { endResult = engine.end(&copy.stream); });
				release.join();
				require(endResult == INFLATE_OK, "cross-thread end");
			}
		});

		runner.test("owned dictionary negotiation", [] {
			const auto dictionary = Payload(1024);
			const auto payload = Payload(8192);
			auto input = compress_zlib_fixture(payload, 15, dictionary);
			for (const auto& engine : s_engines)
			{
				OwnedStream owned{ engine };
				ZlibOracle oracle(15);
				require(engine.init(&owned.stream, 15) == INFLATE_OK, "dictionary init");
				std::vector<uint8_t> actual(payload.size()), expected(payload.size());
				auto& stream = owned.stream;
				const std::array<uint8_t, 2> invalidDictionaryHeader{ 0x78, 0x21 };
				stream.next_in = oracle.stream.next_in = invalidDictionaryHeader.data();
				stream.avail_in = oracle.stream.avail_in = 2;
				stream.next_out = actual.data(); oracle.stream.next_out = expected.data();
				stream.avail_out = oracle.stream.avail_out = static_cast<uint32_t>(actual.size());
				require(engine.inflate(&stream, 4) == INFLATE_DATA_ERROR && oracle.Inflate(4) == INFLATE_DATA_ERROR, "invalid dictionary header");
				require(engine.reset(&stream) == oracle.Reset(), "dictionary reset after header error");
				stream.next_in = oracle.stream.next_in = input.data();
				stream.avail_in = oracle.stream.avail_in = static_cast<uint32_t>(input.size());
				stream.next_out = actual.data(); oracle.stream.next_out = expected.data();
				stream.avail_out = oracle.stream.avail_out = static_cast<uint32_t>(actual.size());
				require(engine.inflate(&stream, 4) == oracle.Inflate(4) && stream.adler == oracle.stream.adler, std::string(engine.name) + " dictionary request");
				require(engine.dictionary(&stream, dictionary) == oracle.SetDictionary(dictionary), std::string(engine.name) + " dictionary install");
				require(engine.inflate(&stream, 4) == oracle.Inflate(4), "dictionary finish");
				SameEnd(stream, oracle.stream, actual.data(), expected.data(), engine.name);
				require(actual == payload, "dictionary bytes");
			}
		});

		runner.test("owned streaming controls preserve decoder history", [] {
			const auto payload = Payload(4096);
			const auto zlib = compress_zlib_fixture(payload, 15);
			const auto gzip = compress_zlib_fixture(payload, 31);
			for (const auto& engine : s_engines)
			{
				OwnedStream owned{ engine };
				auto& stream = owned.stream;
				require(engine.init(&stream, 15) == INFLATE_OK, "control init");
				std::vector<uint8_t> output(payload.size());
				stream.next_in = zlib.data();
				stream.avail_in = static_cast<uint32_t>(zlib.size());
				stream.next_out = output.data();
				stream.avail_out = 1;
				require(engine.inflate(&stream, 0) == INFLATE_OK, "control prefix");
				const auto mark = engine.mark(&stream);
				require(engine.controls ? mark != INFLATE_STREAM_ERROR : mark == INFLATE_STREAM_ERROR, "mark capability");
				int32_t result = INFLATE_OK;
				while (result == INFLATE_OK)
				{
					require(stream.avail_in != 0, "streaming lost pending input");
					stream.avail_out = std::min<uint32_t>(127, static_cast<uint32_t>(payload.size()) - stream.total_out);
					result = engine.inflate(&stream, 0);
				}
				require(result == INFLATE_END && output == payload, std::string(engine.name) + " streaming finish");
				const auto kept = engine.resetKeep(&stream);
				require(kept == (engine.controls ? INFLATE_OK : INFLATE_STREAM_ERROR), "resetKeep capability");
				require(engine.reset2(&stream, 31) == INFLATE_OK, "gzip reset2");
				InflateHeader header{};
				require(engine.header(&stream, &header) == (engine.controls ? INFLATE_OK : INFLATE_STREAM_ERROR), "header capability");
				stream.next_in = gzip.data();
				stream.avail_in = static_cast<uint32_t>(gzip.size());
				stream.next_out = output.data();
				stream.avail_out = static_cast<uint32_t>(output.size());
				require(engine.inflate(&stream, 4) == INFLATE_END && output == payload, "gzip after reset2");
				if (engine.controls)
					require(header.done == 1, "gzip header result not mirrored");
			}
		});

		runner.test("owned sync recovery preserves disabled checksum semantics", [] {
			const auto payload = Payload(4096);
			auto compressed = compress_zlib_fixture(payload, 15, {}, true);
			compressed[2] = (compressed[2] & 0xF9) | 6;
			for (const auto& engine : s_engines)
			{
				OwnedStream owned{ engine };
				ZlibOracle oracle(15);
				require(engine.init(&owned.stream, 15) == INFLATE_OK, "sync init");
				auto& stream = owned.stream;
				auto& expected = oracle.stream;
				std::vector<uint8_t> output(payload.size()), reference(payload.size());
				stream.next_in = expected.next_in = compressed.data();
				stream.avail_in = expected.avail_in = static_cast<uint32_t>(compressed.size());
				stream.next_out = output.data();
				expected.next_out = reference.data();
				stream.avail_out = expected.avail_out = static_cast<uint32_t>(output.size());
				require(engine.inflate(&stream, 0) == INFLATE_DATA_ERROR && oracle.Inflate(0) == INFLATE_DATA_ERROR, "invalid block");
				const auto synced = engine.sync(&stream);
				if (!engine.controls)
				{
					require(synced == INFLATE_STREAM_ERROR, "unsupported sync");
					continue;
				}
				require(synced == oracle.Sync() && synced == INFLATE_OK, "sync recovery");
				require(engine.inflate(&stream, 4) == oracle.Inflate(4), "post-sync return");
				require(stream.total_out == payload.size() / 2 &&
					std::equal(output.begin(), output.begin() + stream.total_out, payload.begin() + payload.size() / 2),
					std::string(engine.name) + " post-sync output");
				SameEnd(stream, expected, output.data(), reference.data(), engine.name);
			}
		});

		runner.test("owned states reject foreign and shallow-copied streams", [] {
			for (const auto& engine : s_engines)
			{
				std::array<uint64_t, 4> foreign{ 0x11223344 };
				Stream view{};
				view.state = foreign.data();
				require(!engine.owned(&view) && !ZlibOwnedState::Find(&view), "foreign detection");
				require(engine.inflate(&view, 0) == INFLATE_STREAM_ERROR && engine.reset(&view) == INFLATE_STREAM_ERROR &&
					engine.end(&view) == INFLATE_STREAM_ERROR && view.state == foreign.data(), "foreign state changed");
				OwnedStream real{ engine };
				real.stream.zalloc = reinterpret_cast<void*>(uintptr_t{ 1 });
				real.stream.zfree = reinterpret_cast<void*>(uintptr_t{ 2 });
				real.stream.opaque = foreign.data();
				require(engine.init(&real.stream, 15) == INFLATE_OK, "owned allocation used engine callbacks");
				require(real.stream.zalloc == reinterpret_cast<void*>(uintptr_t{ 1 }) &&
					real.stream.zfree == reinterpret_cast<void*>(uintptr_t{ 2 }) && real.stream.opaque == foreign.data(), "allocator fields changed");
				auto duplicate = real.stream;
				require(!engine.owned(&duplicate) && engine.end(&duplicate) == INFLATE_STREAM_ERROR, "shallow copy accepted");
				require(engine.owned(&real.stream), "original ownership lost");
				require(engine.reset2(&real.stream, -15) == INFLATE_OK, "reset2 raw");
			}
		});
	}
}
