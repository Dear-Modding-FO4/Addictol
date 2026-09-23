#pragma once

#include <algorithm>
#include <initializer_list>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Addictol::ZlibInflate
{
	constexpr int32_t Z_STREAM_END		= 1;

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

	[[nodiscard]] constexpr bool IsZlibHeader(uint8_t a_cmf, uint8_t a_flg, bool a_allowDictionary = false) noexcept
	{
		return (a_cmf & 0x0F) == 8 &&
			((a_cmf >> 4) & 0x0F) <= 7 &&
			(a_allowDictionary || (a_flg & 0x20) == 0) &&
			((static_cast<uint32_t>(a_cmf) << 8 | a_flg) % 31) == 0;
	}

	[[nodiscard]] inline bool HasZlibHeader(std::span<const uint8_t> a_input) noexcept
	{
		return a_input.size() >= 2 && IsZlibHeader(a_input[0], a_input[1]);
	}

}
