#include "../Addictol/Include/AdTextureStream.h"
#include "Harness.h"

#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
	using namespace Addictol;
	using namespace Addictol::TextureStream;

	struct StreamFixture
	{
		static constexpr size_t kChunks = 4;

		std::array<ChunkDesc, kChunks> chunks{};
		std::array<uint32_t, kChunks> nominal{};
		std::vector<uint8_t> compressed;
		std::vector<std::byte> destination;
		std::array<uint32_t, 2> engineState{ ZlibInflate::MODE_HEAD, 0 };
		uintptr_t vtable{ 0x140ABCDE };
		Detail detail{};
		ResidentState resident{};
		Stream stream{};

		StreamFixture()
		{
			uint32_t inputTotal = 0;
			uint32_t outputTotal = 0;
			for (size_t index = 0; index < kChunks; ++index)
			{
				chunks[index].compressedSize = static_cast<uint32_t>(16 + index * 4);
				chunks[index].uncompressedSize = static_cast<uint32_t>(64 + index * 32);
				nominal[index] = chunks[index].uncompressedSize;
				inputTotal += chunks[index].compressedSize;
				outputTotal += nominal[index];
			}

			compressed.assign(inputTotal, 0);
			destination.assign(outputTotal, std::byte{ 0 });

			detail.vtable = reinterpret_cast<const void*>(vtable);
			detail.directFlag = DETAIL_DIRECT;
			detail.stream.state = engineState.data();

			resident.compressedBase = compressed.data();
			resident.compressedCursor = compressed.data();

			stream.detail = &detail;
			stream.chunks = chunks.data();
			stream.nominalSizes = nominal.data();
			stream.resident = &resident;
			stream.count = kChunks;
			stream.first = 0;
			stream.last = kChunks - 1;
			LayoutMembers();
		}

		// The resident buffer holds only the selected members, starting at the base.
		void LayoutMembers() noexcept
		{
			std::fill(compressed.begin(), compressed.end(), uint8_t{ 0 });
			uint32_t offset = 0;
			for (uint32_t index = stream.first; index <= stream.last; ++index)
			{
				compressed[offset] = 0x78;
				compressed[offset + 1] = 0x9C;
				offset += chunks[index].compressedSize;
			}
		}

		void SetFirst(uint32_t a_first) noexcept
		{
			stream.first = a_first;
			LayoutMembers();
		}

		[[nodiscard]] PreflightResult Run() noexcept
		{
			return Preflight(&stream, destination.data(), vtable);
		}
	};

	struct CallerFixture
	{
		std::vector<uint8_t> code;
		uintptr_t root{ 0 };
		uintptr_t seam{ 0 };

		explicit CallerFixture(const CallerSignature& a_signature, int32_t a_displacement)
		{
			code.assign(0x180, 0xCC);
			root = reinterpret_cast<uintptr_t>(code.data());
			std::memcpy(code.data() + a_signature.preOffset, a_signature.pre.data(), a_signature.pre.size());
			std::memcpy(code.data() + a_signature.returnOffset, a_signature.post.data(), a_signature.post.size());
			code[a_signature.callOffset] = 0xE8;
			for (size_t byte = 0; byte < sizeof(a_displacement); ++byte)
				code[a_signature.callOffset + 1 + byte] =
					static_cast<uint8_t>((static_cast<uint32_t>(a_displacement) >> (8 * byte)) & 0xFF);
			seam = root + a_signature.callOffset + 5 +
				static_cast<uintptr_t>(static_cast<std::intptr_t>(a_displacement));
		}
	};
}

namespace vmm_tests
{
	void run_texture_stream_checks(Runner& runner)
	{
		runner.test("texture stream layout matches the verified engine offsets", [] {
			require(offsetof(Stream, detail) == 0x00, "detail pointer moved");
			require(offsetof(Stream, chunks) == 0x08, "chunk descriptor pointer moved");
			require(offsetof(Stream, nominalSizes) == 0x10, "nominal size array moved");
			require(offsetof(Stream, resident) == 0x18, "resident state moved");
			require(offsetof(Stream, alternate) == 0x20, "alternate state moved");
			require(offsetof(Stream, shadowBuffer) == 0x30, "shadow buffer moved");
			require(offsetof(Stream, shadowSize) == 0x38, "shadow size moved");
			require(sizeof(Stream::shadowSize) == 4, "shadow size is a 32-bit field");
			require(offsetof(Stream, reserved3C) == 0x3C, "the field after shadow size moved");
			require(offsetof(Stream, count) == 0x40, "chunk count moved");
			require(offsetof(Stream, index) == 0x44, "mutable chunk index moved");
			require(offsetof(Stream, first) == 0x4C, "first chunk moved");
			require(offsetof(Stream, last) == 0x50, "last chunk moved");
			require(sizeof(ChunkDesc) == 0x18, "chunk descriptor stride changed");
			require(offsetof(ChunkDesc, compressedSize) == 0x08, "compressed size moved");
			require(offsetof(ChunkDesc, uncompressedSize) == 0x0C, "archive fullSize moved");
			require(offsetof(ResidentState, compressedCursor) == 0x08, "resident cursor moved");
			require(offsetof(Detail, directFlag) == 0x08, "detail mode flag moved");
			require(sizeof(Detail::directFlag) == 1, "the engine compares the detail mode as a byte");
			require(offsetof(Detail, stream) == 0x10, "embedded z_stream moved");
			require(MAX_CHUNK_COUNT == 255, "the chunk count bound changed");
		});

		runner.test("texture address library ids are explicit per runtime", [] {
			require(SEAM_ID.og == 916914, "the OG seam id changed");
			require(SEAM_ID.ng == 2275483 && SEAM_ID.ae == 2275483, "the AE/NG seam id changed");
			require(!SEAM_ID.Shared(), "the seam id is not shared across runtimes");
			require(
				DETAIL_VTABLE_ID.og == 436461 && DETAIL_VTABLE_ID.Shared(),
				"the detail vtable id must be spelled out as the same id on all three runtimes");
			require(
				STREAMING_CALLER_ID.og == 1395021 && STREAMING_CALLER_ID.ng == 2277269 &&
					STREAMING_CALLER_ID.ae == 2277269,
				"the streaming caller ids changed");
			require(
				ARRAY_SLICE_CALLER_ID.og == 9669 && ARRAY_SLICE_CALLER_ID.ng == 2277272 &&
					ARRAY_SLICE_CALLER_ID.ae == 2277272,
				"the array-slice caller ids changed");
			require(
				OG_RESIDENT_INNER_ID.og == 371154 && OG_RESIDENT_INNER_ID.OgOnly(),
				"the resident loop id is OG only");
			require(
				OG_ALTERNATE_INNER_ID.og == 129694 && OG_ALTERNATE_INNER_ID.OgOnly(),
				"the alternate loop id is OG only");
		});

		runner.test("texture seam guards stay exact and relocation free", [] {
			require(Guards::MODERN_ENTRY.size() == 35, "AE/NG entry guard length changed");
			require(Guards::OG_DISPATCHER.size() == 48, "OG dispatcher guard length changed");
			require(Guards::OG_RESIDENT_INNER.size() == 59, "OG resident guard length changed");
			require(Guards::OG_ALTERNATE_INNER.size() == 44, "OG alternate guard length changed");
			require(
				Guards::MODERN_ENTRY_OVERWRITE == 5 && Guards::OG_DISPATCHER_OVERWRITE == 7,
				"the overwritten byte counts changed");

			// Both overwrites must land on register-only instructions, never on a displacement.
			const std::array<uint8_t, 5> modernOverwrite{ 0x40, 0x53, 0x57, 0x41, 0x56 };
			const std::array<uint8_t, 7> ogOverwrite{ 0x48, 0x8B, 0x41, 0x18, 0x48, 0x85, 0xC0 };
			require(
				std::equal(modernOverwrite.begin(), modernOverwrite.end(), Guards::MODERN_ENTRY.begin()),
				"the AE/NG overwrite prefix changed");
			require(
				std::equal(ogOverwrite.begin(), ogOverwrite.end(), Guards::OG_DISPATCHER.begin()),
				"the OG overwrite prefix changed");

			// The resident loop must still pin the entry reset of the mutable chunk index.
			const std::array<uint8_t, 7> residentReset{ 0xC7, 0x42, 0x44, 0x00, 0x00, 0x00, 0x00 };
			require(
				std::equal(residentReset.begin(), residentReset.end(), Guards::OG_RESIDENT_INNER.begin() + 23),
				"the OG resident entry reset moved");
			const std::array<uint8_t, 5> modernReset{ 0xC7, 0x41, 0x44, 0x00, 0x00 };
			require(
				std::equal(modernReset.begin(), modernReset.end(), Guards::MODERN_ENTRY.begin() + 28),
				"the AE/NG entry reset moved");
		});

		runner.test("texture caller sites validate the call target, not just the shape", [] {
			for (const auto& signature : MODERN_CALLERS)
			{
				CallerFixture fixture(signature, 0x2000);
				require(
					static_cast<bool>(ValidateCallerSite(
						fixture.code, signature, fixture.root, fixture.seam)),
					"a well formed AE/NG caller was rejected");
				require(
					!ValidateCallerSite(fixture.code, signature, fixture.root, fixture.seam + 1).targetOk,
					"a call to another target was accepted");
			}

			for (const auto& signature : OG_CALLERS)
			{
				CallerFixture fixture(signature, -0x300);
				const auto control = ValidateCallerSite(
					fixture.code, signature, fixture.root, fixture.seam);
				require(static_cast<bool>(control), "a well formed OG caller was rejected");
				require(control.targetOk, "a negative displacement did not resolve to the seam");

				auto mutated = fixture;
				mutated.root = reinterpret_cast<uintptr_t>(mutated.code.data());
				mutated.code[signature.preOffset] ^= 0x01;
				const auto preBroken = ValidateCallerSite(
					mutated.code, signature, mutated.root, fixture.seam);
				require(!preBroken.preOk, "a mutated pre-call sequence was accepted");
				require(preBroken.postOk, "one mutation invalidated the post-call sequence");

				auto postMutated = fixture;
				postMutated.root = reinterpret_cast<uintptr_t>(postMutated.code.data());
				postMutated.code[signature.returnOffset] ^= 0x01;
				const auto postBroken = ValidateCallerSite(
					postMutated.code, signature, postMutated.root, fixture.seam);
				require(!postBroken.postOk, "a mutated post-call sequence was accepted");
				require(postBroken.preOk, "one mutation invalidated the pre-call sequence");

				auto callMutated = fixture;
				callMutated.root = reinterpret_cast<uintptr_t>(callMutated.code.data());
				callMutated.code[signature.callOffset] = 0xE9;
				require(
					!ValidateCallerSite(callMutated.code, signature, callMutated.root, fixture.seam).callOk,
					"a jump was accepted where a call is required");
			}

			require(
				MODERN_CALLERS[0].caller == BA2Profile::kCallerStreamingTexture &&
					MODERN_CALLERS[1].caller == BA2Profile::kCallerArraySlice &&
					OG_CALLERS[0].caller == BA2Profile::kCallerStreamingTexture &&
					OG_CALLERS[1].caller == BA2Profile::kCallerArraySlice,
				"caller identity was reordered");
			require(
				MODERN_CALLERS[0].returnOffset == 0x13A && MODERN_CALLERS[1].returnOffset == 0x14A &&
					OG_CALLERS[0].returnOffset == 0xAE && OG_CALLERS[1].returnOffset == 0xC9,
				"a caller return offset changed");
		});

		runner.test("texture preflight accepts a complete resident request", [] {
			StreamFixture fixture;
			const auto result = fixture.Run();
			require(static_cast<bool>(result), "a complete request was rejected");
			require(result.bounds.first == 0 && result.bounds.last == 3, "chunk range was rewritten");
			require(result.bounds.count == 4, "chunk count was rewritten");
			require(result.bounds.inputTotal == 16 + 20 + 24 + 28, "input total was miscomputed");
			require(result.bounds.outputStart == 0, "output start was miscomputed");
			require(result.bounds.outputEnd == 64 + 96 + 128 + 160, "selected output end was miscomputed");
			require(result.bounds.outputTotal == 64 + 96 + 128 + 160, "output total was miscomputed");
			require(result.bounds.descMismatches == 0, "a matching descriptor was called a mismatch");
			require(result.bounds.ChunkRows() == 4, "chunk row count was miscomputed");

			fixture.SetFirst(2);
			const auto partial = fixture.Run();
			require(static_cast<bool>(partial), "a partial mip run was rejected");
			require(partial.bounds.inputTotal == 24 + 28,
				"input must start at the resident base, not at chunk zero");
			require(partial.bounds.outputStart == 64 + 96, "output offset must skip earlier mips");
			require(partial.bounds.outputTotal == 64 + 96 + 128 + 160,
				"the caller allocation covers every chunk");
		});

		runner.test("texture preflight delegates unsafe state instead of guessing", [] {
			{
				StreamFixture fixture;
				fixture.stream.resident = nullptr;
				fixture.stream.alternate = &fixture;
				require(fixture.Run().reason == DelegateReason::AlternateState,
					"the alternate state path was not delegated");
			}
			{
				StreamFixture fixture;
				fixture.detail.vtable = reinterpret_cast<const void*>(fixture.vtable + 8);
				require(fixture.Run().reason == DelegateReason::ForeignDetail,
					"a foreign detail vtable was accepted");
			}
			{
				StreamFixture fixture;
				fixture.detail.directFlag = 2;
				require(fixture.Run().reason == DelegateReason::DetailMode,
					"a non-direct detail mode was accepted");
			}
			{
				// Valid objects only write the mode byte, so the bytes above it carry no contract.
				StreamFixture fixture;
				for (auto& byte : fixture.detail.reserved09)
					byte = 0xCD;
				require(static_cast<bool>(fixture.Run()),
					"garbage above the detail mode byte rejected a valid request");
			}
			{
				StreamFixture fixture;
				fixture.engineState[0] = ZlibInflate::MODE_DONE;
				require(fixture.Run().reason == DelegateReason::EngineStream,
					"a mid-stream engine z_stream was accepted");
			}
			{
				StreamFixture fixture;
				fixture.detail.stream.total_out = 1;
				require(fixture.Run().reason == DelegateReason::EngineStream,
					"an engine z_stream with progress was accepted");
			}
			{
				StreamFixture fixture;
				fixture.detail.stream.state = nullptr;
				require(static_cast<bool>(fixture.Run()),
					"a detail that has never decoded must stay eligible");
			}
			{
				StreamFixture fixture;
				fixture.stream.count = 0;
				require(fixture.Run().reason == DelegateReason::ChunkRange,
					"an empty request was accepted");
			}
			{
				StreamFixture fixture;
				fixture.stream.last = fixture.stream.count;
				require(fixture.Run().reason == DelegateReason::ChunkRange,
					"a last chunk outside the count was accepted");
			}
			{
				StreamFixture fixture;
				fixture.stream.first = 3;
				fixture.stream.last = 2;
				require(fixture.Run().reason == DelegateReason::ChunkRange,
					"an inverted chunk range was accepted");
			}
			{
				StreamFixture fixture;
				fixture.stream.count = 256;
				require(fixture.Run().reason == DelegateReason::ChunkRange,
					"a chunk count above the row bound was accepted");
			}
			{
				StreamFixture fixture;
				fixture.chunks[1].compressedSize = 0;
				const auto result = fixture.Run();
				require(result.reason == DelegateReason::ChunkMetadata,
					"an empty compressed member was accepted");
				require(result.zeroCompressed, "the zero compressed member was not disclosed");
			}
			{
				StreamFixture fixture;
				fixture.compressed[16] = 0x78;
				fixture.compressed[17] = 0x9D;
				const auto result = fixture.Run();
				require(result.reason == DelegateReason::ChunkMetadata,
					"a member without an RFC1950 header was accepted");
				require(result.badHeader, "the bad header was not disclosed");
			}
			{
				StreamFixture fixture;
				fixture.nominal[3] = 0xFFFFFFFFu;
				require(fixture.Run().reason == DelegateReason::Arithmetic,
					"a 32-bit output overflow was accepted");
			}
			{
				StreamFixture fixture;
				fixture.chunks[2].compressedSize = 0xFFFFFFFFu;
				require(fixture.Run().reason == DelegateReason::Arithmetic,
					"a 32-bit input overflow was accepted");
			}
			{
				StreamFixture fixture;
				require(Preflight(&fixture.stream, nullptr, fixture.vtable).reason ==
						DelegateReason::ChunkMetadata,
					"a missing destination was accepted");
				require(Preflight(nullptr, fixture.destination.data(), fixture.vtable).reason ==
						DelegateReason::ChunkMetadata,
					"a missing stream was accepted");
			}
			{
				// The member pointers are never formed, let alone read, once the range cannot fit.
				StreamFixture fixture;
				fixture.resident.compressedBase = reinterpret_cast<const uint8_t*>(
					std::numeric_limits<uintptr_t>::max() - 8);
				require(fixture.Run().reason == DelegateReason::Arithmetic,
					"an input range that wraps the address space was accepted");
			}
			{
				StreamFixture fixture;
				auto* destination = reinterpret_cast<std::byte*>(
					std::numeric_limits<uintptr_t>::max() - 16);
				require(Preflight(&fixture.stream, destination, fixture.vtable).reason ==
						DelegateReason::Arithmetic,
					"an output range that wraps the address space was accepted");
			}
		});

		runner.test("texture preflight plans a descriptor disagreement without clearing it to decode", [] {
			StreamFixture fixture;
			fixture.nominal[2] += 1;
			fixture.destination.push_back(std::byte{ 0 });
			const auto result = fixture.Run();
			require(static_cast<bool>(result),
				"a descriptor mismatch is safe to plan; the decode decision belongs to the request");
			require(result.bounds.descMismatches == 1,
				"the descriptor mismatch that makes the request ineligible was not reported");
			require(result.bounds.outputTotal == 64 + 96 + 129 + 160,
				"the caller allocation must follow the nominal size array");

			// A mip this request never touches is not this request's evidence.
			fixture.SetFirst(3);
			const auto partial = fixture.Run();
			require(static_cast<bool>(partial), "the partial request was rejected");
			require(partial.bounds.descMismatches == 0,
				"a descriptor mismatch outside the selected range was counted again");

			fixture.SetFirst(2);
			require(fixture.Run().bounds.descMismatches == 1,
				"a descriptor mismatch inside the selected range was lost");
		});

		runner.test("texture preflight bounds output to the selected range", [] {
			StreamFixture fixture;
			fixture.stream.last = 1;
			fixture.LayoutMembers();
			const auto result = fixture.Run();
			require(static_cast<bool>(result), "a request that stops before the last mip was rejected");
			require(result.bounds.outputStart == 0, "the output start moved");
			require(result.bounds.outputEnd == 64 + 96,
				"the selected output end must stop after the last selected mip");
			require(result.bounds.outputTotal == 64 + 96 + 128 + 160,
				"the caller allocation still covers every mip");
			require(result.bounds.inputTotal == 16 + 20, "unselected members were consumed");

			fixture.SetFirst(1);
			const auto middle = fixture.Run();
			require(middle.bounds.outputStart == 64, "the output start skipped the wrong mips");
			require(middle.bounds.outputEnd == 64 + 96, "the selected end moved with the start");
		});

		runner.test("zlib member headers are validated without reading private state", [] {
			require(ZlibInflate::IsZlibHeader(0x78, 0x9C), "the common zlib header was rejected");
			require(ZlibInflate::IsZlibHeader(0x78, 0x01), "a valid low-level header was rejected");
			require(!ZlibInflate::IsZlibHeader(0x78, 0x9D), "a bad check value was accepted");
			require(!ZlibInflate::IsZlibHeader(0x78, 0x20), "a preset dictionary was accepted");
			require(!ZlibInflate::IsZlibHeader(0x79, 0x9C), "a non-deflate method was accepted");
			require(!ZlibInflate::IsZlibHeader(0x88, 0x98), "an oversized window was accepted");
			const std::array<uint8_t, 1> tooShort{ 0x78 };
			require(!ZlibInflate::HasZlibHeader(tooShort), "a one byte member was accepted");
		});
	}
}
