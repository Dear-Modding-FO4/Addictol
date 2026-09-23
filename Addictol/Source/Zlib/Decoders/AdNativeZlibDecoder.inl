#include <new>

namespace Addictol
{
	namespace nativeZlibDetail
	{
		template<class Api>
		struct Context
		{
			typename Api::Stream stream{};
			typename Api::Header header{};
			InflateHeader* recipient{};
		};

		template<class Destination, class Source>
		void CopyHeader(Destination& a_destination, const Source& a_source) noexcept
		{
			a_destination.text = a_source.text;
			a_destination.time = a_source.time;
			a_destination.xflags = a_source.xflags;
			a_destination.os = a_source.os;
			a_destination.extra = a_source.extra;
			a_destination.extra_len = a_source.extra_len;
			a_destination.extra_max = a_source.extra_max;
			a_destination.name = a_source.name;
			a_destination.name_max = a_source.name_max;
			a_destination.comment = a_source.comment;
			a_destination.comm_max = a_source.comm_max;
			a_destination.hcrc = a_source.hcrc;
			a_destination.done = a_source.done;
		}
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Init(State& a_state, int32_t a_windowBits) noexcept
	{
		if (a_state.context)
			return INFLATE_STREAM_ERROR;
		auto* context = new (std::nothrow) nativeZlibDetail::Context<Api>{};
		if (!context)
			return INFLATE_MEM_ERROR;
		const auto result = Api::init(&context->stream, a_windowBits, Api::version, sizeof(context->stream));
		if (result != INFLATE_OK)
			delete context;
		else
			a_state.context = context;
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Inflate(State& a_state, ZlibInflate::Stream& a_stream, int32_t a_flush) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		const auto result = InvokeNativeInflate(context->stream, a_stream, [&] { return Api::inflate(&context->stream, a_flush); });
		if (context->recipient)
			nativeZlibDetail::CopyHeader(*context->recipient, context->header);
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Reset(State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		const auto result = Api::reset(&context->stream);
		if (result == INFLATE_OK)
			context->recipient = nullptr;
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Reset2(State& a_state, int32_t a_windowBits) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		const auto result = Api::reset2(&context->stream, a_windowBits);
		if (result == INFLATE_OK)
			context->recipient = nullptr;
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::ResetKeep(State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		const auto result = Api::resetKeep(&context->stream);
		if (result == INFLATE_OK)
			context->recipient = nullptr;
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::End(State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		const auto result = Api::end(&context->stream);
		delete context;
		a_state.context = nullptr;
		return result;
	}

	template<class Api>
	bool NativeZlibDecoder<Api>::HasPendingOutput(const State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return false;
		typename Api::Stream probe{};
		if (Api::copy(&probe, &context->stream) != INFLATE_OK)
			return true;
		auto header = context->header;
		if (context->recipient)
			Api::getHeader(&probe, &header);
		uint8_t byte{};
		probe.next_in = nullptr;
		probe.avail_in = 0;
		probe.next_out = &byte;
		probe.avail_out = 1;
		Api::inflate(&probe, 0);
		const bool pending = probe.avail_out == 0;
		Api::end(&probe);
		return pending;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Copy(State& a_destination, const State& a_source) noexcept
	{
		auto* source = static_cast<nativeZlibDetail::Context<Api>*>(a_source.context);
		if (!source || a_destination.context)
			return INFLATE_STREAM_ERROR;
		auto* destination = new (std::nothrow) nativeZlibDetail::Context<Api>{};
		if (!destination)
			return INFLATE_MEM_ERROR;
		const auto result = Api::copy(&destination->stream, &source->stream);
		if (result != INFLATE_OK)
		{
			delete destination;
			return result;
		}
		destination->header = source->header;
		destination->recipient = source->recipient;
		if (destination->recipient)
		{
			const auto header = destination->header;
			Api::getHeader(&destination->stream, &destination->header);
			destination->header = header;
		}
		a_destination.context = destination;
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::SetDictionary(State& a_state, std::span<const uint8_t> a_dictionary) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? Api::setDictionary(&context->stream, a_dictionary.data(), static_cast<uint32_t>(a_dictionary.size())) : INFLATE_STREAM_ERROR;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Sync(State& a_state, ZlibInflate::Stream& a_stream) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? InvokeNativeInflate(context->stream, a_stream, [&] { return Api::sync(&context->stream); }) : INFLATE_STREAM_ERROR;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Prime(State& a_state, int32_t a_bits, int32_t a_value) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? Api::prime(&context->stream, a_bits, a_value) : INFLATE_STREAM_ERROR;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Mark(const State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? Api::mark(&context->stream) : INFLATE_STREAM_ERROR;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::GetHeader(State& a_state, InflateHeader& a_header) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		nativeZlibDetail::CopyHeader(context->header, a_header);
		const auto result = Api::getHeader(&context->stream, &context->header);
		if (result == INFLATE_OK)
		{
			context->recipient = &a_header;
			nativeZlibDetail::CopyHeader(a_header, context->header);
		}
		return result;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::Undermine(State& a_state, int32_t a_allow) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? Api::undermine(&context->stream, a_allow) : INFLATE_STREAM_ERROR;
	}

	template<class Api>
	int32_t NativeZlibDecoder<Api>::SyncPoint(const State& a_state) noexcept
	{
		auto* context = static_cast<nativeZlibDetail::Context<Api>*>(a_state.context);
		return context ? Api::syncPoint(&context->stream) : INFLATE_STREAM_ERROR;
	}
}
