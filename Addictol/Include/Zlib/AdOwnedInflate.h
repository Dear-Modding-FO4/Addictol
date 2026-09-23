#pragma once

#include <Zlib/AdZlibOperationProfile.h>
#include <Zlib/Decoders/AdInflateStreamMirror.h>
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
		std::optional<OperationProfileToken> profileToken;
		OperationProfileSource* profileSource{};
		uint64_t profileElapsedQpc{};

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
				a_state.profileElapsedQpc = 0;
			}
			a_state.policy = ZlibOwnedPolicy::Undecided;
			a_state.outcomePolicy = ZlibOwnedPolicy::Streaming;
			a_state.fallbackReason = WholeInflateDecoder<Whole> ? ZlibFallbackReason::Request : ZlibFallbackReason::NoWhole;
			a_state.mirror.Reset(a_state.windowBits);
			a_stream.total_in = a_stream.total_out = 0;
			a_stream.msg = nullptr;
			a_stream.data_type = 2;
			a_stream.adler = a_state.mirror.InitialChecksum();
		}

		static void ChoosePolicy(State& a_state, ZlibInflate::Stream& a_stream, int32_t a_flush) noexcept
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
				a_state.fallbackReason = ZlibFallbackReason::Capacity;
				if (a_stream.avail_out < a_stream.avail_in)
					return;
				const auto result = Whole::Decode(input, { a_stream.next_out, a_stream.avail_out });
				if (result.status == ZlibDecodeStatus::Success)
				{
					a_state.fallbackReason = ZlibFallbackReason::Decode;
					if (result.consumed < 6 || result.consumed > input.size() || result.produced > a_stream.avail_out)
						return;
					const auto* trailer = input.data() + result.consumed - 4;
					a_stream.adler = uint32_t{ trailer[0] } << 24 | uint32_t{ trailer[1] } << 16 |
						uint32_t{ trailer[2] } << 8 | trailer[3];
					a_stream.next_in += result.consumed;
					a_stream.avail_in -= static_cast<uint32_t>(result.consumed);
					a_stream.total_in += static_cast<uint32_t>(result.consumed);
					a_stream.next_out += result.produced;
					a_stream.avail_out -= static_cast<uint32_t>(result.produced);
					a_stream.total_out += static_cast<uint32_t>(result.produced);
					a_stream.msg = nullptr;
					a_stream.data_type = 64;
					a_state.policy = ZlibOwnedPolicy::Done;
					a_state.outcomePolicy = ZlibOwnedPolicy::Whole;
					a_state.fallbackReason = ZlibFallbackReason::None;
					return;
				}
				// Failed whole attempts leave only unreported output bytes, which streaming may overwrite.
				a_state.fallbackReason = result.codecResult == UINT32_MAX ? ZlibFallbackReason::Allocation :
					result.status == ZlibDecodeStatus::InsufficientSpace ? ZlibFallbackReason::Capacity : ZlibFallbackReason::Decode;
			}
		}

		static bool NativeOperation(State& a_state) noexcept
		{
			if (a_state.policy == ZlibOwnedPolicy::Undecided)
			{
				a_state.policy = ZlibOwnedPolicy::Streaming;
				a_state.outcomePolicy = ZlibOwnedPolicy::Streaming;
				a_state.fallbackReason = ZlibFallbackReason::Request;
			}
			return a_state.policy == ZlibOwnedPolicy::Streaming;
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
			const auto inflate = [&] {
				if (!state || a_flush < 0 || a_flush > 6 || !a_stream->next_out ||
					(a_stream->avail_in && !a_stream->next_in))
					return INFLATE_STREAM_ERROR;
				if (state->policy == ZlibOwnedPolicy::Undecided)
					ChoosePolicy(*state, *a_stream, a_flush);
				if (state->policy == ZlibOwnedPolicy::Done)
					return INFLATE_END;
				return state->mirror.Invoke(*a_stream, [&](ZlibInflate::Stream& a_view) {
					return Streaming::Inflate(state->decoder, a_view, a_flush);
				}, [&] {
					return Streaming::HasPendingOutput(state->decoder);
				});
			};
			if constexpr (Profile::enabled)
				if (state) return Complete(state, a_stream, Profile::Measure(*state, inflate));
			return Complete(state, a_stream, inflate());
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
					const auto result = Streaming::Sync(a_state.decoder, a_view);
					if (result == INFLATE_OK)
						a_state.mirror.AfterSync(a_view.adler);
					return result;
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
