#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "AdProfilerBA2Schema.h"

namespace Addictol::BA2Profile
{
	inline constexpr size_t kChunkRows{ 256 };
	inline constexpr size_t kArenaChunkCount{ 1024 };
	inline constexpr size_t kArenaRowCapacity{ kChunkRows * kArenaChunkCount };
	inline constexpr uint16_t kNoChunk{ 0xFFFF };

	// A texture request never exceeds the stream chunk count, which the fast path pins below 256.
	inline constexpr size_t kMaxRequestChunks{ 255 };
	inline constexpr size_t kMaxBatchRows{ kChunkRows };

	static_assert(kArenaChunkCount < kNoChunk);
	static_assert(kMaxRequestChunks < kMaxBatchRows);
	static_assert(sizeof(CallRecord) * kArenaRowCapacity == 34ull * 1024 * 1024);

	struct RowArena
	{
		CallRecord* rows{ nullptr };
		size_t chunkBudget{ kArenaChunkCount };
		std::array<uint16_t, kArenaChunkCount> chunkNext{};
		std::array<uint16_t, kArenaChunkCount> chunkRows{};
		std::atomic<uint32_t> nextChunk{ 0 };

		void Reset() noexcept { nextChunk.store(0, std::memory_order_relaxed); }
	};

	struct BankCursor
	{
		uint16_t firstChunk{ kNoChunk };
		uint16_t currentChunk{ kNoChunk };
		uint32_t rowsInCurrentChunk{ 0 };
		bool exhausted{ false };

		void Reset() noexcept { *this = BankCursor{}; }
	};

	// Rows of one batch stay contiguous, so a full batch starts a new chunk rather than straddling two.
	[[nodiscard]] inline std::span<CallRecord> ReserveRows(
		RowArena& a_arena,
		BankCursor& a_cursor,
		size_t a_count) noexcept
	{
		if (!a_arena.rows || a_cursor.exhausted || !a_count || a_count > kMaxBatchRows)
			return {};

		if (a_cursor.currentChunk == kNoChunk ||
			kChunkRows - a_cursor.rowsInCurrentChunk < a_count)
		{
			const auto reserved = a_arena.nextChunk.fetch_add(1, std::memory_order_relaxed);
			if (reserved >= a_arena.chunkBudget)
			{
				a_cursor.exhausted = true;
				return {};
			}

			const auto chunk = static_cast<uint16_t>(reserved);
			a_arena.chunkNext[chunk] = kNoChunk;
			a_arena.chunkRows[chunk] = 0;
			if (a_cursor.currentChunk == kNoChunk)
				a_cursor.firstChunk = chunk;
			else
				a_arena.chunkNext[a_cursor.currentChunk] = chunk;
			a_cursor.currentChunk = chunk;
			a_cursor.rowsInCurrentChunk = 0;
		}

		auto* base = &a_arena.rows[static_cast<size_t>(a_cursor.currentChunk) * kChunkRows +
			a_cursor.rowsInCurrentChunk];
		a_cursor.rowsInCurrentChunk += static_cast<uint32_t>(a_count);
		a_arena.chunkRows[a_cursor.currentChunk] =
			static_cast<uint16_t>(a_cursor.rowsInCurrentChunk);
		return { base, a_count };
	}

	template <class Fn>
	void ForEachRow(const RowArena& a_arena, uint16_t a_firstChunk, Fn&& a_fn) noexcept
	{
		if (!a_arena.rows)
			return;

		auto chunk = a_firstChunk;
		while (chunk != kNoChunk)
		{
			const auto rows = static_cast<size_t>(a_arena.chunkRows[chunk]);
			const auto* base = &a_arena.rows[static_cast<size_t>(chunk) * kChunkRows];
			for (size_t row = 0; row < rows; ++row)
				a_fn(base[row]);
			chunk = a_arena.chunkNext[chunk];
		}
	}
}
