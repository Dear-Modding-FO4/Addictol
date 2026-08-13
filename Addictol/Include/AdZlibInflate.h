#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Addictol::ZlibInflate
{
	constexpr std::int32_t Z_STREAM_END = 1;
	constexpr std::int32_t Z_BLOCK = 5;
	constexpr std::int32_t Z_TREES = 6;

	constexpr std::uint32_t MODE_HEAD = 0;
	constexpr std::uint32_t MODE_DONE = 0x1C;
	constexpr std::int32_t DATA_TYPE_DONE = 64;

	struct Stream
	{
		const std::uint8_t* next_in;
		std::uint32_t avail_in;
		std::uint32_t total_in;
		std::uint8_t* next_out;
		std::uint32_t avail_out;
		std::uint32_t total_out;
		const char* msg;
		void* state;
		void* zalloc;
		void* zfree;
		void* opaque;
		std::int32_t data_type;
		std::uint32_t adler;
		std::uint32_t reserved;
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
		constexpr std::size_t MODE_LOAD_OFFSET = 0x78;
		constexpr std::size_t MODE_BOUNDS_OFFSET = 0xA5;
		constexpr std::size_t DONE_STORE_OFFSET = 0x1569;
		constexpr std::size_t RESET_ZERO_OFFSET = 0x1CBE;
		constexpr std::size_t RESET_STORE_OFFSET = 0x1CE5;

		inline constexpr std::array PROLOGUE{
			std::uint8_t{ 0x89 }, std::uint8_t{ 0x54 }, std::uint8_t{ 0x24 }, std::uint8_t{ 0x10 },
			std::uint8_t{ 0x48 }, std::uint8_t{ 0x89 }, std::uint8_t{ 0x4C }, std::uint8_t{ 0x24 },
			std::uint8_t{ 0x08 }, std::uint8_t{ 0x55 }, std::uint8_t{ 0x41 }, std::uint8_t{ 0x54 },
			std::uint8_t{ 0x41 }, std::uint8_t{ 0x55 }, std::uint8_t{ 0x48 }, std::uint8_t{ 0x8B },
			std::uint8_t{ 0xEC }, std::uint8_t{ 0x48 }, std::uint8_t{ 0x81 }, std::uint8_t{ 0xEC },
			std::uint8_t{ 0x80 }, std::uint8_t{ 0x00 }, std::uint8_t{ 0x00 }, std::uint8_t{ 0x00 },
			std::uint8_t{ 0x4C }, std::uint8_t{ 0x8B }, std::uint8_t{ 0xE1 }, std::uint8_t{ 0x48 },
			std::uint8_t{ 0x85 }, std::uint8_t{ 0xC9 }
		};
		inline constexpr std::array MODE_LOAD{
			std::uint8_t{ 0x41 }, std::uint8_t{ 0x8B }, std::uint8_t{ 0x45 }, std::uint8_t{ 0x00 }
		};
		inline constexpr std::array MODE_BOUNDS{
			std::uint8_t{ 0x83 }, std::uint8_t{ 0xF8 }, std::uint8_t{ 0x1E }
		};
		inline constexpr std::array DONE_STORE{
			std::uint8_t{ 0x41 }, std::uint8_t{ 0xC7 }, std::uint8_t{ 0x45 }, std::uint8_t{ 0x00 },
			std::uint8_t{ 0x1C }, std::uint8_t{ 0x00 }, std::uint8_t{ 0x00 }, std::uint8_t{ 0x00 }
		};
		inline constexpr std::array RESET_ZERO{
			std::uint8_t{ 0x45 }, std::uint8_t{ 0x33 }, std::uint8_t{ 0xC0 }
		};
		inline constexpr std::array RESET_STORE{
			std::uint8_t{ 0x4C }, std::uint8_t{ 0x89 }, std::uint8_t{ 0x02 },
			std::uint8_t{ 0x44 }, std::uint8_t{ 0x89 }, std::uint8_t{ 0x42 }, std::uint8_t{ 0x0C }
		};

		constexpr std::size_t VALIDATION_SIZE = RESET_STORE_OFFSET + RESET_STORE.size();
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

	template<std::size_t N>
	[[nodiscard]] inline bool Matches(
		std::span<const std::uint8_t> a_code,
		std::size_t a_offset,
		const std::array<std::uint8_t, N>& a_expected) noexcept
	{
		return a_offset <= a_code.size() &&
			a_expected.size() <= a_code.size() - a_offset &&
			std::equal(a_expected.begin(), a_expected.end(), a_code.begin() + a_offset);
	}

	[[nodiscard]] inline ContractValidation ValidateContract(
		std::span<const std::uint8_t> a_code) noexcept
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

	[[nodiscard]] inline volatile std::uint32_t* ModePointer(const Stream& a_stream) noexcept
	{
		return static_cast<volatile std::uint32_t*>(a_stream.state);
	}

	[[nodiscard]] inline volatile std::uint32_t* LastPointer(const Stream& a_stream) noexcept
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

	[[nodiscard]] inline std::uint32_t ReadBigEndian32(const std::uint8_t* a_bytes) noexcept
	{
		return (static_cast<std::uint32_t>(a_bytes[0]) << 24) |
			(static_cast<std::uint32_t>(a_bytes[1]) << 16) |
			(static_cast<std::uint32_t>(a_bytes[2]) << 8) |
			static_cast<std::uint32_t>(a_bytes[3]);
	}

	[[nodiscard]] inline bool CommitCompletedStream(
		Stream* a_stream,
		const void* a_expectedState,
		std::size_t a_consumed,
		std::size_t a_produced) noexcept
	{
		if (!a_stream || !a_expectedState || a_stream->state != a_expectedState ||
			!a_stream->next_in || !a_stream->next_out ||
			a_stream->total_in || a_stream->total_out || a_consumed < sizeof(std::uint32_t) ||
			a_consumed > a_stream->avail_in || a_produced > a_stream->avail_out)
			return false;

		auto* mode = ModePointer(*a_stream);
		if (*mode != MODE_HEAD)
			return false;

		const auto consumed = static_cast<std::uint32_t>(a_consumed);
		const auto produced = static_cast<std::uint32_t>(a_produced);
		const auto adler = ReadBigEndian32(a_stream->next_in + consumed - sizeof(std::uint32_t));

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
