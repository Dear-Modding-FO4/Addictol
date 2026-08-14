#pragma once

#include <algorithm>
#include <initializer_list>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Addictol::ZlibInflate
{
	constexpr int32_t Z_STREAM_END		= 1;
	constexpr int32_t Z_BLOCK			= 5;
	constexpr int32_t Z_TREES			= 6;

	constexpr uint32_t MODE_HEAD		= 0;
	constexpr uint32_t MODE_DONE		= 0x1C;
	constexpr int32_t DATA_TYPE_DONE	= 64;

	struct Stream
	{
		const uint8_t* next_in;
		uint32_t avail_in;
		uint32_t total_in;
		uint8_t* next_out;
		uint32_t avail_out;
		uint32_t total_out;
		const char* msg;
		void* state;
		void* zalloc;
		void* zfree;
		void* opaque;
		int32_t data_type;
		uint32_t adler;
		uint32_t reserved;
	};

	static_assert(offsetof(Stream, next_in) == 0x00);
	static_assert(offsetof(Stream, avail_in) == 0x08);
	static_assert(offsetof(Stream, total_in) == 0x0C);
	static_assert(offsetof(Stream, next_out) == 0x10);
	static_assert(offsetof(Stream, avail_out) == 0x18);
	static_assert(offsetof(Stream, total_out) == 0x1C);
	static_assert(offsetof(Stream, msg) == 0x20);
	static_assert(offsetof(Stream, state) == 0x28);
	static_assert(offsetof(Stream, data_type) == 0x48);
	static_assert(offsetof(Stream, adler) == 0x4C);
	static_assert(sizeof(Stream) == 0x58);

	namespace Contract
	{
		inline constexpr size_t MODE_LOAD_OFFSET	= 0x78;
		inline constexpr size_t MODE_BOUNDS_OFFSET	= 0xA5;
		inline constexpr size_t DONE_STORE_OFFSET	= 0x1569;
		inline constexpr size_t RESET_ZERO_OFFSET	= 0x1CBE;
		inline constexpr size_t RESET_STORE_OFFSET	= 0x1CE5;

		inline constexpr std::initializer_list<uint8_t> PROLOGUE		{ 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24,
			0x08, 0x55, 0x41, 0x54, 0x41, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8B,
			0xE1, 0x48, 0x85, 0xC9 };
		inline constexpr std::initializer_list<uint8_t> MODE_LOAD		{ 0x41, 0x8B, 0x45, 0x00 };
		inline constexpr std::initializer_list<uint8_t> MODE_BOUNDS		{ 0x83, 0xF8, 0x1E };
		inline constexpr std::initializer_list<uint8_t> DONE_STORE		{ 0x41, 0xC7, 0x45, 0x00, 0x1C, 0x00, 0x00, 0x00 };
		inline constexpr std::initializer_list<uint8_t> RESET_ZERO		{ 0x45, 0x33, 0xC0 };
		inline constexpr std::initializer_list<uint8_t> RESET_STORE		{ 0x4C, 0x89, 0x02, 0x44, 0x89, 0x42, 0x0C };

		inline constexpr size_t VALIDATION_SIZE = RESET_STORE_OFFSET + RESET_STORE.size();
	}

	struct ContractValidation
	{
		bool prologue;
		bool modeLoad;
		bool modeBounds;
		bool doneStore;
		bool resetZero;
		bool resetStore;

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return prologue && modeLoad && modeBounds && doneStore && resetZero && resetStore;
		}
	};

	[[nodiscard]] inline bool Matches(std::span<const uint8_t> a_code, size_t a_offset, const std::initializer_list<uint8_t>& a_expected) noexcept
	{
		return a_offset <= a_code.size() &&
			a_expected.size() <= a_code.size() - a_offset &&
			std::equal(a_expected.begin(), a_expected.end(), a_code.begin() + a_offset);
	}

	[[nodiscard]] inline ContractValidation ValidateContract(
		std::span<const uint8_t> a_code) noexcept
	{
		return {
			Matches(a_code, 0, Contract::PROLOGUE),
			Matches(a_code, Contract::MODE_LOAD_OFFSET, Contract::MODE_LOAD),
			Matches(a_code, Contract::MODE_BOUNDS_OFFSET, Contract::MODE_BOUNDS),
			Matches(a_code, Contract::DONE_STORE_OFFSET, Contract::DONE_STORE),
			Matches(a_code, Contract::RESET_ZERO_OFFSET, Contract::RESET_ZERO),
			Matches(a_code, Contract::RESET_STORE_OFFSET, Contract::RESET_STORE)
		};
	}

	[[nodiscard]] inline volatile uint32_t* ModePointer(const Stream& a_stream) noexcept
	{
		return static_cast<volatile uint32_t*>(a_stream.state);
	}

	[[nodiscard]] inline volatile uint32_t* LastPointer(const Stream& a_stream) noexcept
	{
		return ModePointer(a_stream) + 1;
	}

	[[nodiscard]] inline bool CanAttempt(const Stream* a_stream, std::int32_t a_flush) noexcept
	{
		if (!a_stream || !a_stream->state || !a_stream->next_in || !a_stream->next_out ||
			!a_stream->avail_in || !a_stream->avail_out || a_stream->total_in || a_stream->total_out ||
			a_flush == Z_BLOCK || a_flush == Z_TREES)
			return false;

		return *ModePointer(*a_stream) == MODE_HEAD;
	}

	[[nodiscard]] inline uint32_t ReadBigEndian32(const uint8_t* a_bytes) noexcept
	{
		return (static_cast<uint32_t>(a_bytes[0]) << 24) |
			(static_cast<uint32_t>(a_bytes[1]) << 16) |
			(static_cast<uint32_t>(a_bytes[2]) << 8) |
			static_cast<uint32_t>(a_bytes[3]);
	}

	[[nodiscard]] inline bool CommitCompletedStream(
		Stream* a_stream,
		const void* a_expectedState,
		std::size_t a_consumed,
		std::size_t a_produced) noexcept
	{
		if (!a_stream || !a_expectedState || a_stream->state != a_expectedState ||
			!a_stream->next_in || !a_stream->next_out ||
			a_stream->total_in || a_stream->total_out || a_consumed < sizeof(uint32_t) ||
			a_consumed > a_stream->avail_in || a_produced > a_stream->avail_out)
			return false;

		auto* mode = ModePointer(*a_stream);
		if (*mode != MODE_HEAD)
			return false;

		const auto consumed = static_cast<uint32_t>(a_consumed);
		const auto produced = static_cast<uint32_t>(a_produced);
		const auto adler = ReadBigEndian32(a_stream->next_in + consumed - sizeof(uint32_t));

		a_stream->next_in += consumed;
		a_stream->avail_in -= consumed;
		a_stream->total_in += consumed;
		a_stream->next_out += produced;
		a_stream->avail_out -= produced;
		a_stream->total_out += produced;
		a_stream->msg = nullptr;
		a_stream->adler = adler;
		a_stream->data_type = DATA_TYPE_DONE;

		*LastPointer(*a_stream) = 1;
		std::atomic_signal_fence(std::memory_order_release);
		*mode = MODE_DONE;
		return true;
	}
}
