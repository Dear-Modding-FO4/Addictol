#pragma once

#include <Zlib/AdZlibOperationProfile.h>
#include <Zlib/AdInflateBuffer.h>
#include <Zlib/Decoders/AdInflateStreamMirror.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <optional>

namespace Addictol
{
	struct ZlibOwnedState
	{
		inline static constexpr uint64_t MAGIC = 0x4164496E666C6174;
		uint64_t magic{ MAGIC };
		ZlibInflate::Stream* owner{};
		const void* implementation{};
		int32_t windowBits{};
		ZlibOwnedPolicy policy{ ZlibOwnedPolicy::Undecided };
		ZlibOwnedPolicy outcomePolicy{ ZlibOwnedPolicy::Streaming };
		ZlibFallbackReason fallbackReason{ ZlibFallbackReason::None };
		InflateStreamMirror mirror;
		InflateBuffer output;
		InflateBuffer input;
		size_t produced{}, served{}, consumed{};
		bool inputAccounted{}, materialized{};
		bool privateInput{};
		uint32_t replayOffset{}, replayTotal{};
		std::optional<OperationProfileToken> profileToken;
		OperationProfileSource* profileSource{};

		static bool IsTagged(const ZlibInflate::Stream* a_stream) noexcept
		{
			if (!a_stream || !a_stream->state)
				return false;
			uint64_t tag{};
			std::memcpy(&tag, a_stream->state, sizeof(tag));
			return tag == MAGIC;
		}
		static ZlibOwnedState* Find(const ZlibInflate::Stream* a_stream) noexcept
		{
			if (!IsTagged(a_stream))
				return nullptr;
			auto* state = static_cast<ZlibOwnedState*>(a_stream->state);
			return state->owner == a_stream ? state : nullptr;
		}
	};

	template<class Whole, StreamingInflateDecoder Streaming, class Profile = NoZlibStreamProfile>
		requires (WholeInflateDecoder<Whole> || std::same_as<Whole, NoWholeInflateDecoder>)
	struct OwnedInflate
	{
	private:
		inline static const uint8_t s_identity{};
		struct State : ZlibOwnedState
		{
			typename Streaming::State decoder{};
			bool decoderLive{};
			~State() noexcept
			{
				if (decoderLive)
					Streaming::End(decoder);
			}
		};

		static State* Find(const ZlibInflate::Stream* a_stream) noexcept
		{
			auto* state = ZlibOwnedState::Find(a_stream);
			return state && state->implementation == &s_identity ? static_cast<State*>(state) : nullptr;
		}

		static void ResetView(State& a_state, ZlibInflate::Stream& a_stream) noexcept
		{
			if constexpr (Profile::enabled)
			{
				Profile::End(a_state, a_stream.total_out);
				a_state.profileToken.reset();
				a_state.profileSource = nullptr;
			}
			a_state.policy = ZlibOwnedPolicy::Undecided;
			a_state.outcomePolicy = ZlibOwnedPolicy::Streaming;
			a_state.fallbackReason = WholeInflateDecoder<Whole> ? ZlibFallbackReason::Request : ZlibFallbackReason::NoWhole;
			a_state.output.Reset();
			a_state.input.Reset();
			a_state.produced = a_state.served = a_state.consumed = 0;
			a_state.inputAccounted = a_state.materialized = false;
			a_state.privateInput = false;
			a_state.replayOffset = a_state.replayTotal = 0;
			a_state.mirror.Reset(a_state.windowBits);
			a_stream.total_in = a_stream.total_out = 0;
			a_stream.msg = nullptr;
			a_stream.data_type = 2;
			a_stream.adler = a_state.mirror.InitialChecksum();
		}

		static void ChoosePolicy(State& a_state, const ZlibInflate::Stream& a_stream, int32_t a_flush) noexcept
		{
			a_state.policy = ZlibOwnedPolicy::Streaming;
			a_state.fallbackReason = ZlibFallbackReason::NoWhole;
			if constexpr (WholeInflateDecoder<Whole>)
			{
				const auto input = std::span{ a_stream.next_in, a_stream.avail_in };
				a_state.fallbackReason = ZlibFallbackReason::Format;
				if (a_state.windowBits < 0 || a_state.windowBits > 15 || !ZlibInflate::HasZlibHeader(input) ||
					(a_state.windowBits && (input[0] >> 4) + 8 > a_state.windowBits))
					return;
				a_state.fallbackReason = ZlibFallbackReason::Request;
				if (a_flush > 4 || !a_stream.avail_out)
					return;
				size_t capacity = std::clamp<size_t>(a_stream.avail_out, 64 * 1024, InflateBuffer::MAX_CAPACITY);
				for (;;)
				{
					a_state.fallbackReason = ZlibFallbackReason::Allocation;
					if (!a_state.output.Acquire(capacity))
						break;
					const auto result = Whole::Decode(input, a_state.output.Bytes());
					if (result.status == ZlibDecodeStatus::Success)
					{
						if (result.consumed < 6 || result.consumed > input.size() ||
							result.produced > a_state.output.Bytes().size() ||
							!a_state.input.Acquire(result.consumed))
							break;
						std::memcpy(a_state.input.Bytes().data(), input.data(), result.consumed);
						a_state.consumed = result.consumed;
						a_state.produced = result.produced;
						a_state.policy = result.produced <= a_stream.avail_out ? ZlibOwnedPolicy::Whole : ZlibOwnedPolicy::Buffered;
						a_state.outcomePolicy = a_state.policy;
						a_state.fallbackReason = ZlibFallbackReason::None;
						return;
					}
					a_state.fallbackReason = result.codecResult == UINT32_MAX ? ZlibFallbackReason::Allocation :
						result.status == ZlibDecodeStatus::InsufficientSpace ? ZlibFallbackReason::Capacity : ZlibFallbackReason::Decode;
					if (result.status != ZlibDecodeStatus::InsufficientSpace || capacity == InflateBuffer::MAX_CAPACITY)
						break;
					capacity = std::min(capacity * 2, InflateBuffer::MAX_CAPACITY);
				}
				a_state.output.Reset();
				a_state.input.Reset();
			}
		}

		static int32_t ServeBuffer(State& a_state, ZlibInflate::Stream& a_view, int32_t a_flush) noexcept
		{
			if (!a_state.inputAccounted)
			{
				a_view.next_in += a_state.consumed;
				a_view.avail_in -= static_cast<uint32_t>(a_state.consumed);
				a_view.total_in += static_cast<uint32_t>(a_state.consumed);
				a_state.inputAccounted = true;
			}
			const auto count = std::min<size_t>(a_view.avail_out, a_state.produced - a_state.served);
			if (count)
				std::memcpy(a_view.next_out, a_state.output.Bytes().data() + a_state.served, count);
			a_state.served += count;
			a_view.next_out += count;
			a_view.avail_out -= static_cast<uint32_t>(count);
			a_view.total_out += static_cast<uint32_t>(count);
			a_view.msg = nullptr;
			if (a_state.served == a_state.produced)
			{
				a_state.policy = ZlibOwnedPolicy::Done;
				a_view.data_type = 64;
				return INFLATE_END;
			}
			a_view.data_type = 0;
			return a_flush == 4 || !count ? INFLATE_BUF_ERROR : INFLATE_OK;
		}

		static bool NativeOperation(State& a_state) noexcept
		{
			if (a_state.policy == ZlibOwnedPolicy::Undecided)
			{
				a_state.policy = ZlibOwnedPolicy::Streaming;
				a_state.outcomePolicy = ZlibOwnedPolicy::Streaming;
				a_state.fallbackReason = ZlibFallbackReason::Request;
			}
			if (a_state.policy == ZlibOwnedPolicy::Streaming || a_state.materialized)
				return true;
			// Reconstruct native history only when a control operation needs the skipped parser state.
			ZlibInflate::Stream replay{};
			replay.next_in = a_state.input.Bytes().data();
			replay.avail_in = static_cast<uint32_t>(a_state.consumed);
			std::array<uint8_t, 32768> discard{};
			int32_t result = INFLATE_OK;
			while (result == INFLATE_OK && (replay.total_out < a_state.served ||
				(a_state.served == a_state.produced && result != INFLATE_END)))
			{
				replay.next_out = discard.data();
				replay.avail_out = static_cast<uint32_t>(std::min<size_t>(discard.size(),
					std::max<size_t>(1, a_state.served - replay.total_out)));
				const auto before = replay.total_out;
				result = Streaming::Inflate(a_state.decoder, replay, 0);
				if (result == INFLATE_OK && replay.total_out == before)
					return false;
			}
			a_state.materialized = (result == INFLATE_OK || result == INFLATE_END) && replay.total_out == a_state.served;
			if (a_state.materialized)
			{
				a_state.policy = ZlibOwnedPolicy::Streaming;
				a_state.outcomePolicy = ZlibOwnedPolicy::Streaming;
				a_state.fallbackReason = ZlibFallbackReason::Request;
				a_state.privateInput = result != INFLATE_END;
				a_state.replayOffset = static_cast<uint32_t>(a_state.consumed - replay.avail_in);
				a_state.replayTotal = replay.total_in;
			}
			return a_state.materialized;
		}

		template<class Function>
		static int32_t InvokeDecoder(State& a_state, ZlibInflate::Stream& a_view, Function&& a_function) noexcept
		{
			if (!a_state.privateInput)
				return a_function(a_view);
			const auto* input = a_view.next_in;
			const auto available = a_view.avail_in;
			const auto total = a_view.total_in;
			a_view.next_in = a_state.input.Bytes().data() + a_state.replayOffset;
			a_view.avail_in = static_cast<uint32_t>(a_state.consumed) - a_state.replayOffset;
			a_view.total_in = a_state.replayTotal;
			const auto result = a_function(a_view);
			a_state.replayOffset = static_cast<uint32_t>(a_state.consumed) - a_view.avail_in;
			a_state.replayTotal = a_view.total_in;
			a_view.next_in = input;
			a_view.avail_in = available;
			a_view.total_in = total;
			if (result == INFLATE_END)
				a_state.privateInput = false;
			return result;
		}

		static int32_t Complete(State* a_state, const ZlibInflate::Stream* a_stream, int32_t a_result) noexcept
		{
			if constexpr (Profile::enabled)
				if (a_state && (a_result == INFLATE_END || (a_result < 0 && a_result != INFLATE_BUF_ERROR)))
					Profile::End(*a_state, a_stream->total_out);
			return a_result;
		}

		template<bool Supported, class Function>
		static int32_t Control(ZlibInflate::Stream* a_stream, Function&& a_function) noexcept
		{
			auto* state = Find(a_stream);
			if constexpr (Supported)
			{
				if (state && NativeOperation(*state))
				{
					const auto result = a_function(*state);
					return result < 0 ? Complete(state, a_stream, result) : result;
				}
			}
			return Complete(state, a_stream, INFLATE_STREAM_ERROR);
		}

	public:
		static bool IsOwned(const ZlibInflate::Stream* a_stream) noexcept { return Find(a_stream) != nullptr; }

		static int32_t Init(ZlibInflate::Stream* a_stream, const char* a_version = "1", int32_t a_size = sizeof(ZlibInflate::Stream)) noexcept
		{
			return Init2(a_stream, 15, a_version, a_size);
		}

		static int32_t Init2(ZlibInflate::Stream* a_stream, int32_t a_windowBits,
			const char* a_version = "1", int32_t a_size = sizeof(ZlibInflate::Stream)) noexcept
		{
			if (!a_version || a_version[0] != '1' || a_size != sizeof(ZlibInflate::Stream))
				return INFLATE_VERSION_ERROR;
			if (!a_stream)
				return INFLATE_STREAM_ERROR;
			auto* state = new (std::nothrow) State{};
			if (!state)
				return INFLATE_MEM_ERROR;
			const auto result = Streaming::Init(state->decoder, a_windowBits);
			if (result != INFLATE_OK)
			{
				delete state;
				return result;
			}
			state->owner = a_stream;
			state->decoderLive = true;
			state->implementation = &s_identity;
			state->windowBits = a_windowBits;
			ResetView(*state, *a_stream);
			a_stream->state = static_cast<ZlibOwnedState*>(state);
			return INFLATE_OK;
		}

		static int32_t Inflate(ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept
		{
			auto* state = Find(a_stream);
			if constexpr (Profile::enabled)
				if (state) Profile::Begin(*state);
			if (!state || a_flush < 0 || a_flush > 6 || !a_stream->next_out ||
				(a_stream->avail_in && !a_stream->next_in))
				return Complete(state, a_stream, INFLATE_STREAM_ERROR);
			if (state->policy == ZlibOwnedPolicy::Undecided)
				ChoosePolicy(*state, *a_stream, a_flush);
			if (state->policy != ZlibOwnedPolicy::Streaming && a_flush > 4 && !NativeOperation(*state))
				return Complete(state, a_stream, INFLATE_STREAM_ERROR);
			const auto result = state->mirror.Invoke(*a_stream, [&](ZlibInflate::Stream& a_view) {
				return state->policy == ZlibOwnedPolicy::Streaming ?
					InvokeDecoder(*state, a_view, [&](ZlibInflate::Stream& a_nativeView) {
						return Streaming::Inflate(state->decoder, a_nativeView, a_flush);
					}) : ServeBuffer(*state, a_view, a_flush);
			}, [&] {
				return state->policy == ZlibOwnedPolicy::Streaming ?
					state->privateInput || Streaming::HasPendingOutput(state->decoder) : state->served < state->produced;
			});
			return Complete(state, a_stream, result);
		}

		static int32_t Reset(ZlibInflate::Stream* a_stream) noexcept
		{
			auto* state = Find(a_stream);
			if (!state)
				return INFLATE_STREAM_ERROR;
			const auto result = Streaming::Reset(state->decoder);
			if (result == INFLATE_OK)
				ResetView(*state, *a_stream);
			return Complete(state, a_stream, result);
		}

		static int32_t Reset2(ZlibInflate::Stream* a_stream, int32_t a_windowBits) noexcept
		{
			auto* state = Find(a_stream);
			if (!state)
				return INFLATE_STREAM_ERROR;
			const auto result = Streaming::Reset2(state->decoder, a_windowBits);
			if (result == INFLATE_OK)
			{
				state->windowBits = a_windowBits;
				ResetView(*state, *a_stream);
			}
			return Complete(state, a_stream, result);
		}

		static int32_t ResetKeep(ZlibInflate::Stream* a_stream) noexcept
		{
			auto* state = Find(a_stream);
			if constexpr (Profile::enabled)
				if (state) Profile::End(*state, a_stream->total_out);
			if constexpr (Streaming::capabilities.resetKeep)
			{
				if (!state || !NativeOperation(*state))
					return INFLATE_STREAM_ERROR;
				const auto result = Streaming::ResetKeep(state->decoder);
				if (result == INFLATE_OK)
				{
					ResetView(*state, *a_stream);
					state->policy = ZlibOwnedPolicy::Streaming;
					state->fallbackReason = ZlibFallbackReason::Request;
				}
				return result;
			}
			return INFLATE_STREAM_ERROR;
		}

		static int32_t End(ZlibInflate::Stream* a_stream) noexcept
		{
			auto* state = Find(a_stream);
			if (!state)
				return INFLATE_STREAM_ERROR;
			const auto result = Streaming::End(state->decoder);
			if constexpr (Profile::enabled)
				Profile::End(*state, a_stream->total_out);
			state->decoderLive = false;
			state->magic = 0;
			delete state;
			a_stream->state = nullptr;
			return result;
		}

		static int32_t Copy(ZlibInflate::Stream* a_destination, const ZlibInflate::Stream* a_source) noexcept
		{
			if constexpr (Streaming::capabilities.copy)
			{
				auto* source = Find(a_source);
				if (!source || !a_destination)
					return INFLATE_STREAM_ERROR;
				auto* destination = new (std::nothrow) State{};
				if (!destination)
					return INFLATE_MEM_ERROR;
				for (auto pair : { std::pair{ &destination->output, &source->output }, std::pair{ &destination->input, &source->input } })
				{
					if (!pair.second->Bytes().empty())
					{
						if (!pair.first->Acquire(pair.second->Bytes().size()))
						{
							delete destination;
							return INFLATE_MEM_ERROR;
						}
						std::memcpy(pair.first->Bytes().data(), pair.second->Bytes().data(), pair.second->Bytes().size());
					}
				}
				const auto result = Streaming::Copy(destination->decoder, source->decoder);
				if (result != INFLATE_OK) { delete destination; return result; }
				destination->decoderLive = true;
				destination->owner = a_destination;
				destination->implementation = &s_identity;
				destination->windowBits = source->windowBits;
				destination->policy = source->policy;
				destination->outcomePolicy = source->outcomePolicy;
				destination->fallbackReason = source->fallbackReason;
				destination->mirror = source->mirror;
				destination->produced = source->produced;
				destination->served = source->served;
				destination->consumed = source->consumed;
				destination->inputAccounted = source->inputAccounted;
				destination->materialized = source->materialized;
				destination->privateInput = source->privateInput;
				destination->replayOffset = source->replayOffset;
				destination->replayTotal = source->replayTotal;
				*a_destination = *a_source;
				a_destination->state = static_cast<ZlibOwnedState*>(destination);
				return INFLATE_OK;
			}
			return INFLATE_STREAM_ERROR;
		}

		static int32_t SetDictionary(ZlibInflate::Stream* a_stream, std::span<const uint8_t> a_dictionary) noexcept
		{
			return Control<Streaming::capabilities.setDictionary>(a_stream, [&]<class S>(S& a_state) {
				return Streaming::SetDictionary(a_state.decoder, a_dictionary);
			});
		}

		static int32_t GetHeader(ZlibInflate::Stream* a_stream, InflateHeader* a_header) noexcept
		{
			if (!a_header)
				return Complete(Find(a_stream), a_stream, INFLATE_STREAM_ERROR);
			return Control<Streaming::capabilities.getHeader>(a_stream, [&]<class S>(S& a_state) {
				return Streaming::GetHeader(a_state.decoder, *a_header);
			});
		}

		static int32_t Prime(ZlibInflate::Stream* a_stream, int32_t a_bits, int32_t a_value) noexcept
		{
			return Control<Streaming::capabilities.prime>(a_stream, [&]<class S>(S& a_state) {
				return Streaming::Prime(a_state.decoder, a_bits, a_value);
			});
		}

		static int32_t Mark(ZlibInflate::Stream* a_stream) noexcept
		{
			if constexpr (Streaming::capabilities.mark)
			{
				auto* state = Find(a_stream);
				if (state && NativeOperation(*state))
					return Streaming::Mark(state->decoder);
			}
			return Complete(Find(a_stream), a_stream, INFLATE_STREAM_ERROR);
		}

		static int32_t Sync(ZlibInflate::Stream* a_stream) noexcept
		{
			return Control<Streaming::capabilities.sync>(a_stream, [&]<class S>(S& a_state) {
				return a_state.mirror.Invoke(*a_stream, [&](ZlibInflate::Stream& a_view) {
					return InvokeDecoder(a_state, a_view, [&](ZlibInflate::Stream& a_nativeView) {
						const auto result = Streaming::Sync(a_state.decoder, a_nativeView);
						if (result == INFLATE_OK)
							a_state.mirror.AfterSync(a_nativeView.adler);
						return result;
					});
				}, [] { return false; }, false);
			});
		}

		static int32_t SyncPoint(ZlibInflate::Stream* a_stream) noexcept
		{
			return Control<Streaming::capabilities.syncPoint>(a_stream, [&]<class S>(S& a_state) {
				return Streaming::SyncPoint(a_state.decoder);
			});
		}

		static int32_t Undermine(ZlibInflate::Stream* a_stream, int32_t a_allow) noexcept
		{
			return Control<Streaming::capabilities.undermine>(a_stream, [&]<class S>(S& a_state) {
				return Streaming::Undermine(a_state.decoder, a_allow);
			});
		}
	};
}
