#pragma once

#include <Zlib/AdZlibInflate.h>

#include <array>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace Addictol
{
	enum class ZlibStreamResult : uint8_t
	{
		InputComplete,
		InputRefilled,
		Abandoned,
		Evicted
	};

	inline constexpr std::array ZLIB_STREAM_RESULTS{
		std::pair{ ZlibStreamResult::InputComplete, std::string_view{ "input_complete" } },
		std::pair{ ZlibStreamResult::InputRefilled, std::string_view{ "input_refilled" } },
		std::pair{ ZlibStreamResult::Abandoned, std::string_view{ "untracked.abandoned" } },
		std::pair{ ZlibStreamResult::Evicted, std::string_view{ "untracked.evicted" } }
	};

	struct ZlibStreamInput
	{
		const void* state{ nullptr };
		uintptr_t end{ 0 };
		uint32_t available{ 0 };
		bool start{ false };

		[[nodiscard]] static ZlibStreamInput Read(const ZlibInflate::Stream& a_stream) noexcept;
	};

	struct ZlibStreamProgress
	{
		uintptr_t initialInputEnd{ 0 };
		uint32_t initialAvailable{ 0 };
		bool refilled{ false };
		uint64_t totalOutput{ 0 };

		[[nodiscard]] ZlibStreamResult Classify(uint32_t a_totalInput) const noexcept;
	};

	// Payloads are constructed in-place; move-only tokens need no move assignment.
	// Retirement callbacks run under the lock and must not re-enter the tracker.
	template<class Payload, size_t Capacity = 256>
	class ZlibStreamTracker
	{
	public:
		static_assert(Capacity > 0);

		template<class Retire>
		void Start(const ZlibStreamInput& a_input, Payload a_payload, Retire&& a_retire) noexcept
		{
			std::scoped_lock lock{ m_mutex };
			auto* slot = Find(a_input.state);
			if (slot)
				RetireEntry(*slot, ZlibStreamResult::Abandoned, a_retire);
			else
			{
				for (auto& entry : m_entries)
				{
					if (!entry)
					{
						slot = &entry;
						break;
					}
				}
			}
			if (!slot)
			{
				slot = &m_entries.front();
				for (auto& entry : m_entries)
				{
					if (entry->order < (*slot)->order)
						slot = &entry;
				}
				RetireEntry(*slot, ZlibStreamResult::Evicted, a_retire);
			}
			slot->emplace(a_input, ++m_order, std::move(a_payload));
		}

		template<class Retire>
		void Abandon(const void* a_state, Retire&& a_retire) noexcept
		{
			std::scoped_lock lock{ m_mutex };
			if (auto* slot = Find(a_state))
				RetireEntry(*slot, ZlibStreamResult::Abandoned, a_retire);
		}

		template<class Retire>
		void FinishCall(const ZlibStreamInput& a_input, uint32_t a_totalInput,
			uint32_t a_totalOutput, int32_t a_result, Retire&& a_retire) noexcept
		{
			std::scoped_lock lock{ m_mutex };
			if (auto* slot = Find(a_input.state))
			{
				auto& progress = (*slot)->progress;
				progress.refilled |= a_input.end != progress.initialInputEnd;
				progress.totalOutput = a_totalOutput;
				if (a_result == ZlibInflate::Z_STREAM_END)
					RetireEntry(*slot, progress.Classify(a_totalInput), a_retire);
				// Z_OK and Z_BUF_ERROR can both continue with another window.
				else if (a_result != 0 && a_result != -5)
					RetireEntry(*slot, ZlibStreamResult::Abandoned, a_retire);
			}
		}

		template<class Retire>
		void Clear(Retire&& a_retire) noexcept
		{
			DiscardIf([](const Payload&) { return true; }, a_retire);
		}

		template<class Predicate, class Retire>
		void DiscardIf(Predicate&& a_predicate, Retire&& a_retire) noexcept
		{
			std::scoped_lock lock{ m_mutex };
			for (auto& slot : m_entries)
			{
				if (slot && a_predicate(slot->payload))
					RetireEntry(slot, ZlibStreamResult::Abandoned, a_retire);
			}
		}

	private:
		struct Entry
		{
			Entry(const ZlibStreamInput& a_input, uint64_t a_order, Payload a_payload) noexcept :
				state(a_input.state),
				order(a_order),
				progress{ a_input.end, a_input.available },
				payload(std::move(a_payload))
			{}

			const void* state;
			uint64_t order;
			ZlibStreamProgress progress;
			Payload payload;
		};

		[[nodiscard]] std::optional<Entry>* Find(const void* a_state) noexcept
		{
			for (auto& slot : m_entries)
			{
				if (slot && slot->state == a_state)
					return &slot;
			}
			return nullptr;
		}

		template<class Retire>
		static void RetireEntry(std::optional<Entry>& a_slot, ZlibStreamResult a_result,
			Retire& a_retire) noexcept
		{
			a_retire(a_slot->payload, a_slot->progress, a_result);
			a_slot.reset();
		}

		std::mutex m_mutex;
		std::array<std::optional<Entry>, Capacity> m_entries{};
		uint64_t m_order{ 0 };
	};
}
