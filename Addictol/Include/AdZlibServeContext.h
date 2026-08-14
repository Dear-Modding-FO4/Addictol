#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "AdProfilerBA2Schema.h"
#include "AdProfilerBA2Rows.h"

namespace Addictol
{
	// How an inflate call nested inside a texture request must be served.
	enum class ZlibServeMode : uint8_t
	{
		Normal,
		ForceStockObserved,
		CaptureReplay
	};

	struct ZlibReplayChunk
	{
		uint64_t qpc{ 0 };
		uint32_t consumed{ 0 };
		uint32_t produced{ 0 };
		uint32_t calls{ 0 };
		int32_t zlibResult{ 0 };
	};

	// Stock replay of one texture request, grouped by the live chunk index the engine is writing.
	struct ZlibReplayCapture
	{
		std::array<ZlibReplayChunk, BA2Profile::kMaxBatchRows> chunks{};
		const volatile uint32_t* liveChunkIndex{ nullptr };
		uint32_t unattributedCalls{ 0 };
		uint32_t overflowedSums{ 0 };
		bool timingEnabled{ false };

		// Rows are only honest if every replayed byte landed on a known chunk.
		[[nodiscard]] bool AttributionOk() const noexcept
		{
			return !unattributedCalls && !overflowedSums;
		}

		void Account(
			uint64_t a_qpc,
			uint32_t a_consumed,
			uint32_t a_produced,
			int32_t a_result) noexcept
		{
			const auto index = liveChunkIndex ? *liveChunkIndex : BA2Profile::kMaxBatchRows;
			if (index >= chunks.size())
			{
				++unattributedCalls;
				return;
			}

			auto& chunk = chunks[index];
			constexpr auto byteLimit = std::numeric_limits<uint32_t>::max();
			if (a_consumed > byteLimit - chunk.consumed ||
				a_produced > byteLimit - chunk.produced ||
				chunk.calls == std::numeric_limits<uint32_t>::max())
			{
				++overflowedSums;
				return;
			}

			chunk.qpc += a_qpc;
			chunk.consumed += a_consumed;
			chunk.produced += a_produced;
			chunk.zlibResult = a_result;
			++chunk.calls;
		}
	};

	struct ZlibServeState
	{
		ZlibServeMode mode{ ZlibServeMode::Normal };
		ZlibReplayCapture* capture{ nullptr };
		BA2Profile::CallerId callerId{ BA2Profile::kCallerNone };
		uint64_t requestSequence{ 0 };
		uint64_t streamAddress{ 0 };
		uint32_t depth{ 0 };

		[[nodiscard]] bool Active() const noexcept { return depth != 0; }
	};

	[[nodiscard]] inline ZlibServeState& CurrentZlibServe() noexcept
	{
		thread_local ZlibServeState state{};
		return state;
	}

	// Depth-aware and restoring, so nested engine work never inherits a stale mode.
	class ZlibServeScope
	{
		ZlibServeState& m_state;
		ZlibServeState m_previous;

		ZlibServeScope(const ZlibServeScope&) = delete;
		ZlibServeScope& operator=(const ZlibServeScope&) = delete;

	public:
		ZlibServeScope(
			ZlibServeMode a_mode,
			BA2Profile::CallerId a_callerId,
			uint64_t a_requestSequence,
			uint64_t a_streamAddress,
			ZlibReplayCapture* a_capture = nullptr) noexcept :
			m_state(CurrentZlibServe()),
			m_previous(m_state)
		{
			m_state.mode = a_mode;
			m_state.capture = a_capture;
			m_state.callerId = a_callerId;
			m_state.requestSequence = a_requestSequence;
			m_state.streamAddress = a_streamAddress;
			m_state.depth = m_previous.depth + 1;
		}

		~ZlibServeScope() noexcept { m_state = m_previous; }
	};
}
