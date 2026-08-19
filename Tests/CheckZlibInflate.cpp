#include "../Addictol/Include/Zlib/AdZlibInflate.h"
#include "Harness.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

namespace
{
	using Addictol::ZlibInflate::ContractValidation;
	using Addictol::ZlibInflate::Stream;

	struct State
	{
		std::uint32_t mode;
		std::uint32_t last;
	};

	void FillContract(std::vector<std::uint8_t>& a_code)
	{
		const auto write = [&a_code](std::size_t a_offset, std::initializer_list<std::uint8_t> a_bytes) {
			std::copy(a_bytes.begin(), a_bytes.end(), a_code.begin() + a_offset);
		};

		write(0x0, { 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55,
			0x41, 0x54, 0x41, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC,
			0x80, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xE1, 0x48, 0x85, 0xC9 });
		write(0x78, { 0x41, 0x8B, 0x45, 0x00 });
		write(0xA5, { 0x83, 0xF8, 0x1E });
		write(0x1569, { 0x41, 0xC7, 0x45, 0x00, 0x1C, 0x00, 0x00, 0x00 });
		write(0x1CBE, { 0x45, 0x33, 0xC0 });
		write(0x1CE5, { 0x4C, 0x89, 0x02, 0x44, 0x89, 0x42, 0x0C });
	}

	Stream MakeStream(
		State& a_state,
		const std::uint8_t* a_input,
		std::size_t a_inputSize,
		std::uint8_t* a_output,
		std::size_t a_outputSize)
	{
		Stream stream{};
		stream.next_in = a_input;
		stream.avail_in = static_cast<std::uint32_t>(a_inputSize);
		stream.next_out = a_output;
		stream.avail_out = static_cast<std::uint32_t>(a_outputSize);
		stream.msg = "stale";
		stream.state = &a_state;
		stream.data_type = 2;
		return stream;
	}

	void RequireUnchanged(
		const Stream& a_stream,
		const Stream& a_before,
		const State& a_state,
		const State& a_stateBefore)
	{
		using vmm_tests::require;
		require(a_stream.next_in == a_before.next_in, "next_in changed on rejection");
		require(a_stream.avail_in == a_before.avail_in, "avail_in changed on rejection");
		require(a_stream.total_in == a_before.total_in, "total_in changed on rejection");
		require(a_stream.next_out == a_before.next_out, "next_out changed on rejection");
		require(a_stream.avail_out == a_before.avail_out, "avail_out changed on rejection");
		require(a_stream.total_out == a_before.total_out, "total_out changed on rejection");
		require(a_stream.msg == a_before.msg, "msg changed on rejection");
		require(a_stream.data_type == a_before.data_type, "data_type changed on rejection");
		require(a_stream.adler == a_before.adler, "adler changed on rejection");
		require(a_state.mode == a_stateBefore.mode, "private mode changed on rejection");
	}
}

namespace vmm_tests
{
	void run_zlib_inflate_checks(Runner& runner)
	{
		using namespace Addictol::ZlibInflate;

		runner.test("zlib contract signatures validate independently", [] {
			std::vector<std::uint8_t> code(0x1CEC);
			FillContract(code);
			require(static_cast<bool>(ValidateContract(code)), "complete contract was rejected");

			code[0x1569] ^= 1;
			const ContractValidation validation = ValidateContract(code);
			require(!validation.doneStore, "mutated DONE store was accepted");
			require(
				validation.prologue && validation.modeLoad && validation.modeBounds &&
					validation.resetZero && validation.resetStore,
				"one mutation invalidated unrelated signatures");
		});

		runner.test("zlib whole-buffer eligibility is state-derived", [] {
			std::array<std::uint8_t, 8> input{};
			std::array<std::uint8_t, 8> output{};
			State state{ MODE_HEAD, 0 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());

			require(CanAttempt(&stream, 2), "pristine HEAD stream was rejected");
			state.mode = MODE_DONE;
			require(!CanAttempt(&stream, 2), "DONE stream was accepted");
			state.mode = MODE_HEAD;
			stream.total_in = 1;
			require(!CanAttempt(&stream, 2), "progressed stream was accepted");
			stream.total_in = 0;
			require(!CanAttempt(&stream, Z_BLOCK), "Z_BLOCK was accepted");

			std::uint64_t backPointer = 0x1122334455667788ull;
			stream.state = &backPointer;
			require(!CanAttempt(&stream, 2), "non-zero state back-pointer looked like HEAD");
		});

		runner.test("zlib completion preserves remainders and accumulates totals", [] {
			const std::array<std::uint8_t, 10> input{
				0x78, 0x9C, 0x01, 0x02, 0x12, 0x34, 0x56, 0x78, 0xAA, 0xBB
			};
			std::array<std::uint8_t, 16> output{};
			State state{ MODE_HEAD, 0 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());

			require(
				CommitCompletedStream(&stream, &state, 8, 5),
				"valid completion was rejected");
			require(stream.next_in == input.data() + 8, "next_in did not advance by consumed bytes");
			require(stream.avail_in == 2, "trailing input was not preserved");
			require(stream.total_in == 8, "total_in did not accumulate consumed bytes");
			require(stream.next_out == output.data() + 5, "next_out did not advance by produced bytes");
			require(stream.avail_out == 11, "spare output was not preserved");
			require(stream.total_out == 5, "total_out did not accumulate produced bytes");
			require(stream.msg == nullptr, "success did not clear msg");
			require(stream.adler == 0x12345678, "zlib trailer Adler-32 was decoded incorrectly");
			require(stream.data_type == DATA_TYPE_DONE, "terminal data_type was not published");
			require(state.last == 1, "private last flag was not committed");
			require(state.mode == MODE_DONE, "private mode was not committed to DONE");
			require(!CanAttempt(&stream, 2), "completed stream remained eligible");
		});

		runner.test("zlib completion rejects invalid state and bounds without mutation", [] {
			const std::array<std::uint8_t, 8> input{
				0x78, 0x9C, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78
			};
			std::array<std::uint8_t, 8> output{};

			const auto checkRejected = [&](std::uint32_t a_mode, std::size_t a_consumed, std::size_t a_produced) {
				State state{ a_mode, 0 };
				auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
				const auto before = stream;
				const auto stateBefore = state;
				require(
					!CommitCompletedStream(&stream, &state, a_consumed, a_produced),
					"invalid completion was accepted");
				RequireUnchanged(stream, before, state, stateBefore);
			};

			checkRejected(MODE_DONE, 8, 4);
			checkRejected(MODE_HEAD, 3, 3);
			checkRejected(MODE_HEAD, 9, 3);
			checkRejected(MODE_HEAD, 8, 9);

			State state{ MODE_HEAD, 0 };
			auto stream = MakeStream(state, input.data(), input.size(), output.data(), output.size());
			State otherState{ MODE_HEAD, 0 };
			const auto before = stream;
			const auto stateBefore = state;
			require(
				!CommitCompletedStream(&stream, &otherState, 8, 4),
				"changed private-state pointer was accepted");
			RequireUnchanged(stream, before, state, stateBefore);
		});
	}
}
