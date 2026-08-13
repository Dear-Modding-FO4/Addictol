#include "../Addictol/Include/AdZlibBackend.h"
#include "Harness.h"

#include <array>

namespace
{
	using namespace Addictol;

	struct State
	{
		std::uint32_t mode;
		std::uint32_t last;
	};

	struct FakeLibDeflateBackend
	{
		inline static constexpr auto kind = ZlibBackendKind::LibDeflate;
		inline static bool prepared = true;
		inline static int prepareCalls = 0;
		inline static int decodeCalls = 0;
		inline static ZlibDecodeResult decodeResult{
			ZlibDecodeStatus::Success, 8, 4
		};

		static bool Prepare() noexcept
		{
			++prepareCalls;
			return prepared;
		}

		static ZlibDecodeResult Decode(
			std::span<const std::uint8_t>,
			std::span<std::uint8_t>) noexcept
		{
			++decodeCalls;
			return decodeResult;
		}

		static void Reset() noexcept
		{
			prepared = true;
			prepareCalls = 0;
			decodeCalls = 0;
			decodeResult = { ZlibDecodeStatus::Success, 8, 4 };
		}
	};

	struct TestClock
	{
		std::uint64_t value{ 0 };
		int calls{ 0 };

		std::uint64_t operator()() noexcept
		{
			++calls;
			value += 10;
			return value;
		}
	};

	ZlibInflate::Stream MakeStream(
		State& a_state,
		const std::uint8_t* a_input,
		std::size_t a_inputSize,
		std::uint8_t* a_output,
		std::size_t a_outputSize)
	{
		ZlibInflate::Stream stream{};
		stream.next_in = a_input;
		stream.avail_in = static_cast<std::uint32_t>(a_inputSize);
		stream.next_out = a_output;
		stream.avail_out = static_cast<std::uint32_t>(a_outputSize);
		stream.state = &a_state;
		return stream;
	}
}

namespace vmm_tests
{
	void run_zlib_backend_checks(Runner& runner)
	{
		runner.test("zlib backend names and parser are complete", [] {
			require(DEFAULT_ZLIB_BACKEND == ZlibBackendKind::LibDeflate, "default backend changed");
			require(ParseZlibBackend("stock") == ZlibBackendKind::Stock, "stock did not parse");
			require(
				ParseZlibBackend("libdeflate") == ZlibBackendKind::LibDeflate,
				"libdeflate did not parse");
			require(!ParseZlibBackend(""), "empty backend parsed");
			require(!ParseZlibBackend("LibDeflate"), "backend parsing became case-insensitive");
			require(!ParseZlibBackend("zlib-ng"), "unavailable backend parsed");
			require(ZlibBackendKindName(ZlibBackendKind::Stock) == "stock", "stock name mismatch");
			require(
				ZlibBackendKindName(ZlibBackendKind::LibDeflate) == "libdeflate",
				"libdeflate name mismatch");
			require(ZlibBackendRegistryId(ZlibBackendKind::Stock) == 1, "stock registry ID changed");
			require(
				ZlibBackendRegistryId(ZlibBackendKind::LibDeflate) == 2,
				"libdeflate registry ID changed");
			require(
				ZlibFallbackReasonRegistryId(ZlibFallbackReason::Commit) == 4,
				"fallback registry IDs changed");
			require(
				ZLIB_BACKEND_NAMES[0].name != ZLIB_BACKEND_NAMES[1].name,
				"backend names are not unique");
		});

		runner.test("selected stock delegates without a codec attempt", [] {
			std::array<std::uint8_t, 8> input{};
			std::array<std::uint8_t, 8> output{};
			State state{ ZlibInflate::MODE_HEAD, 0 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
			int stockCalls = 0;
			TestClock clock;

			const auto outcome = ServeZlib<StockZlibBackend>(
				&stream,
				2,
				[&](ZlibInflate::Stream* a_stream, std::int32_t) noexcept {
					++stockCalls;
					a_stream->next_in += 3;
					a_stream->avail_in -= 3;
					a_stream->next_out += 5;
					a_stream->avail_out -= 5;
					return 0;
				},
				true,
				1'000'000,
				[&]() noexcept { return clock(); });

			require(stockCalls == 1, "stock backend did not call original inflate");
			require(outcome.primaryBackendId == 1, "primary stock registry ID mismatch");
			require(outcome.primaryAttempted, "selected stock was not marked attempted");
			require(outcome.primaryQpc == 10, "stock primary timing mismatch");
			require(outcome.fallbackBackendId == 0, "stock reported a fallback backend");
			require(outcome.fallbackReasonId == 0, "stock reported fallback reason");
			require(outcome.fallbackQpc == 0, "stock reported fallback timing");
			require(outcome.servedBackendId == 1, "served stock registry ID mismatch");
			require(outcome.totalQpc == 30, "stock total timing mismatch");
			require(outcome.qpcFrequency == 1'000'000, "stock QPC frequency mismatch");
			require(outcome.consumed == 3 && outcome.produced == 5, "stock byte deltas mismatch");
		});

		runner.test("libdeflate backend reports a completed serve", [] {
			const std::array<std::uint8_t, 8> input{
				0x78, 0x9C, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78
			};
			std::array<std::uint8_t, 8> output{};
			State state{ ZlibInflate::MODE_HEAD, 0 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
			int stockCalls = 0;
			TestClock clock;
			FakeLibDeflateBackend::Reset();

			const auto outcome = ServeZlib<FakeLibDeflateBackend>(
				&stream,
				2,
				[&](ZlibInflate::Stream*, std::int32_t) noexcept {
					++stockCalls;
					return 0;
				},
				true,
				1'000'000,
				[&]() noexcept { return clock(); });

			require(stockCalls == 0, "successful codec called stock");
			require(FakeLibDeflateBackend::prepareCalls == 1, "codec was not prepared");
			require(FakeLibDeflateBackend::decodeCalls == 1, "codec was not called");
			require(outcome.primaryBackendId == 2, "primary libdeflate registry ID mismatch");
			require(outcome.primaryAttempted, "libdeflate was not marked attempted");
			require(outcome.primaryQpc == 10, "libdeflate primary timing mismatch");
			require(outcome.fallbackBackendId == 0, "codec success reported fallback backend");
			require(outcome.fallbackReasonId == 0, "codec success reported fallback reason");
			require(outcome.servedBackendId == 2, "codec success service ID mismatch");
			require(outcome.totalQpc == 40, "libdeflate total timing mismatch");
			require(outcome.consumed == 8 && outcome.produced == 4, "codec byte counts mismatch");
			require(state.mode == ZlibInflate::MODE_DONE, "codec success did not commit DONE");
		});

		runner.test("libdeflate state rejection reports stock service", [] {
			std::array<std::uint8_t, 8> input{};
			std::array<std::uint8_t, 8> output{};
			State state{ ZlibInflate::MODE_DONE, 1 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
			int stockCalls = 0;
			TestClock clock;
			FakeLibDeflateBackend::Reset();

			const auto outcome = ServeZlib<FakeLibDeflateBackend>(
				&stream,
				2,
				[&](ZlibInflate::Stream*, std::int32_t) noexcept {
					++stockCalls;
					return ZlibInflate::Z_STREAM_END;
				},
				true,
				1'000'000,
				[&]() noexcept { return clock(); });

			require(stockCalls == 1, "state rejection did not call stock");
			require(FakeLibDeflateBackend::prepareCalls == 0, "state rejection prepared codec");
			require(!outcome.primaryAttempted, "state rejection marked primary attempted");
			require(outcome.primaryQpc == 0, "state rejection timed primary");
			require(outcome.fallbackBackendId == 1, "state fallback backend mismatch");
			require(outcome.fallbackReasonId == 1, "state reason mismatch");
			require(outcome.fallbackQpc == 10, "state fallback timing mismatch");
			require(outcome.servedBackendId == 1, "state rejection service mismatch");
			require(outcome.totalQpc == 30, "state fallback total timing mismatch");
		});

		runner.test("libdeflate fallback reasons remain distinct", [] {
			const std::array<std::uint8_t, 8> input{
				0x78, 0x9C, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78
			};
			std::array<std::uint8_t, 8> output{};

			const auto run = [&](bool a_prepared, ZlibDecodeResult a_decoded) {
				State state{ ZlibInflate::MODE_HEAD, 0 };
				auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
				FakeLibDeflateBackend::Reset();
				FakeLibDeflateBackend::prepared = a_prepared;
				FakeLibDeflateBackend::decodeResult = a_decoded;
				TestClock clock;
				return ServeZlib<FakeLibDeflateBackend>(
					&stream,
					2,
					[](ZlibInflate::Stream*, std::int32_t) noexcept { return 0; },
					true,
					1'000'000,
					[&]() noexcept { return clock(); });
			};

			const auto allocation = run(false, {});
			require(allocation.fallbackReasonId == 2, "allocation fallback reason mismatch");
			require(
				allocation.primaryQpc == 10 && allocation.fallbackQpc == 10 &&
					allocation.totalQpc == 50,
				"allocation timing split mismatch");

			const auto decode = run(true, { ZlibDecodeStatus::Failed });
			require(decode.fallbackReasonId == 3, "decode fallback reason mismatch");
			require(
				decode.primaryQpc == 10 && decode.fallbackQpc == 10 &&
					decode.totalQpc == 60,
				"decode timing split mismatch");

			const auto commit = run(true, { ZlibDecodeStatus::Success, 3, 3 });
			require(commit.fallbackReasonId == 4, "commit fallback reason mismatch");
			require(
				commit.primaryQpc == 10 && commit.fallbackQpc == 10 &&
					commit.totalQpc == 60,
				"commit timing split mismatch");
		});

		runner.test("disabled zlib timing leaves raw QPC fields empty", [] {
			std::array<std::uint8_t, 8> input{};
			std::array<std::uint8_t, 8> output{};
			State state{ ZlibInflate::MODE_DONE, 1 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
			TestClock clock;
			FakeLibDeflateBackend::Reset();

			const auto outcome = ServeZlib<FakeLibDeflateBackend>(
				&stream,
				2,
				[](ZlibInflate::Stream*, std::int32_t) noexcept {
					return ZlibInflate::Z_STREAM_END;
				},
				false,
				1'000'000,
				[&]() noexcept { return clock(); });

			require(clock.calls == 0, "disabled timing read QPC");
			require(
				outcome.primaryQpc == 0 && outcome.fallbackQpc == 0 &&
					outcome.totalQpc == 0 && outcome.qpcFrequency == 0,
				"disabled timing published raw ticks");
		});
	}
}
