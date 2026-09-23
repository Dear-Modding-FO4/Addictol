#include <Zlib/Decoders/AdIsalDecoder.h>
#include <Zlib/Decoders/AdInflateStreamMirror.h>
#include <igzip_lib.h>
#include <libdeflate/libdeflate.h>
#include <new>

namespace Addictol
{
	namespace isalDecoderDetail
	{
		struct Context
		{
			inflate_state stream{};
			int32_t windowBits{};
			int32_t error{};
			bool autoHeader{};
			bool headerPending{};
			bool needsDictionary{};
			uint8_t prefix[6]{};
			uint8_t prefixSize{};
		};

		bool ValidWindowBits(int32_t a_bits) noexcept
		{
			if (a_bits < 0)
				return a_bits >= -15 && a_bits <= -8;
			return a_bits < 48 && ((a_bits & 15) == 0 || (a_bits & 15) >= 8);
		}

		void Initialize(Context& a_context, int32_t a_bits) noexcept
		{
			isal_inflate_init(&a_context.stream);
			a_context.windowBits = a_bits;
			a_context.error = 0;
			a_context.autoHeader = a_bits >= 32;
			a_context.headerPending = a_bits >= 0;
			a_context.prefixSize = 0;
			a_context.needsDictionary = false;
			a_context.stream.crc_flag = a_bits < 0 ? ISAL_DEFLATE :
				a_bits >= 16 && a_bits < 32 ? ISAL_GZIP : ISAL_ZLIB;
			const auto bits = a_bits < 0 ? -a_bits : a_bits & 15;
			a_context.stream.hist_bits = bits ? bits : 15;
		}
	}

	int32_t IsalDecoder::Init(State& a_state, int32_t a_windowBits) noexcept
	{
		if (a_state.context || !isalDecoderDetail::ValidWindowBits(a_windowBits))
			return INFLATE_STREAM_ERROR;
		auto* context = new (std::nothrow) isalDecoderDetail::Context{};
		if (!context)
			return INFLATE_MEM_ERROR;
		isalDecoderDetail::Initialize(*context, a_windowBits);
		a_state.context = context;
		return INFLATE_OK;
	}

	int32_t IsalDecoder::Inflate(State& a_state, ZlibInflate::Stream& a_stream, int32_t a_flush) noexcept
	{
		auto* context = static_cast<isalDecoderDetail::Context*>(a_state.context);
		if (!context || a_flush < 0 || a_flush > 4 || !a_stream.next_out ||
			(a_stream.avail_in && !a_stream.next_in))
			return INFLATE_STREAM_ERROR;
		if (context->error)
			return context->error;
		if (context->needsDictionary)
		{
			a_stream.adler = context->stream.dict_id;
			return INFLATE_NEED_DICT;
		}
		const auto inputBefore = a_stream.avail_in;
		const auto outputBefore = a_stream.avail_out;
		const auto totalBefore = a_stream.total_in;
		if (context->headerPending)
		{
			while (context->prefixSize < 2 && a_stream.avail_in)
			{
				context->prefix[context->prefixSize++] = *a_stream.next_in++;
				--a_stream.avail_in;
				++a_stream.total_in;
			}
			if (context->prefixSize < 2)
				return a_flush == 4 || inputBefore == a_stream.avail_in ? INFLATE_BUF_ERROR : INFLATE_OK;
			if (context->autoHeader)
				context->stream.crc_flag = context->prefix[0] == 0x1F && context->prefix[1] == 0x8B ? ISAL_GZIP : ISAL_ZLIB;
			if (context->stream.crc_flag == ISAL_ZLIB &&
				!ZlibInflate::IsZlibHeader(context->prefix[0], context->prefix[1], true))
			{
				a_stream.msg = "invalid zlib header";
				context->error = INFLATE_DATA_ERROR;
				return context->error;
			}
			if (context->stream.crc_flag == ISAL_ZLIB && static_cast<uint32_t>((context->prefix[0] >> 4) + 8) > context->stream.hist_bits)
			{
				a_stream.msg = "invalid window size";
				context->error = INFLATE_DATA_ERROR;
				return context->error;
			}
			const uint8_t needed = context->stream.crc_flag == ISAL_ZLIB && (context->prefix[1] & 0x20) ? 6 : 2;
			while (context->prefixSize < needed && a_stream.avail_in)
			{
				context->prefix[context->prefixSize++] = *a_stream.next_in++;
				--a_stream.avail_in;
				++a_stream.total_in;
			}
			if (context->prefixSize < needed)
				return a_flush == 4 || inputBefore == a_stream.avail_in ? INFLATE_BUF_ERROR : INFLATE_OK;
			context->headerPending = false;
			context->stream.next_in = context->prefix;
			context->stream.avail_in = needed;
			context->stream.next_out = a_stream.next_out;
			context->stream.avail_out = a_stream.avail_out;
			const auto prefixResult = isal_inflate(&context->stream);
			if (prefixResult < 0)
			{
				context->error = INFLATE_DATA_ERROR;
				return context->error;
			}
			if (prefixResult == ISAL_NEED_DICT)
			{
				context->needsDictionary = true;
				a_stream.total_in = totalBefore;
				a_stream.adler = context->stream.dict_id;
				return INFLATE_NEED_DICT;
			}
		}
		const auto result = InvokeNativeInflate(context->stream, a_stream, [&] { return isal_inflate(&context->stream); });
		a_stream.msg = nullptr;
		a_stream.data_type = context->stream.block_state == ISAL_BLOCK_FINISH ? 64 : 0;
		if (result == ISAL_NEED_DICT)
		{
			context->needsDictionary = true;
			a_stream.total_in = totalBefore;
			a_stream.adler = context->stream.dict_id;
			return INFLATE_NEED_DICT;
		}
		if (result < 0)
		{
			a_stream.msg = result == ISAL_INCORRECT_CHECKSUM ? "incorrect data check" : "invalid compressed data";
			context->error = INFLATE_DATA_ERROR;
			return context->error;
		}
		if (context->stream.block_state == ISAL_BLOCK_FINISH)
			return INFLATE_END;
		return a_flush == 4 || (inputBefore == a_stream.avail_in && outputBefore == a_stream.avail_out) ?
			INFLATE_BUF_ERROR : INFLATE_OK;
	}

	int32_t IsalDecoder::Reset(State& a_state) noexcept
	{
		auto* context = static_cast<isalDecoderDetail::Context*>(a_state.context);
		if (!context)
			return INFLATE_STREAM_ERROR;
		isalDecoderDetail::Initialize(*context, context->windowBits);
		return INFLATE_OK;
	}

	int32_t IsalDecoder::Reset2(State& a_state, int32_t a_windowBits) noexcept
	{
		auto* context = static_cast<isalDecoderDetail::Context*>(a_state.context);
		if (!context || !isalDecoderDetail::ValidWindowBits(a_windowBits))
			return INFLATE_STREAM_ERROR;
		isalDecoderDetail::Initialize(*context, a_windowBits);
		return INFLATE_OK;
	}

	int32_t IsalDecoder::End(State& a_state) noexcept
	{
		if (!a_state.context)
			return INFLATE_STREAM_ERROR;
		delete static_cast<isalDecoderDetail::Context*>(a_state.context);
		a_state.context = nullptr;
		return INFLATE_OK;
	}

	bool IsalDecoder::HasPendingOutput(const State& a_state) noexcept
	{
		const auto* context = static_cast<const isalDecoderDetail::Context*>(a_state.context);
		if (!context || context->headerPending || context->needsDictionary || context->error)
			return false;
		if (context->stream.tmp_out_valid > context->stream.tmp_out_processed)
			return true;
		auto* probe = new (std::nothrow) isalDecoderDetail::Context(*context);
		if (!probe)
			return true;
		uint8_t byte{};
		probe->stream.next_in = nullptr;
		probe->stream.avail_in = 0;
		probe->stream.next_out = &byte;
		probe->stream.avail_out = 1;
		isal_inflate(&probe->stream);
		const bool pending = probe->stream.avail_out == 0;
		delete probe;
		return pending;
	}

	int32_t IsalDecoder::Copy(State& a_destination, const State& a_source) noexcept
	{
		if (a_destination.context || !a_source.context)
			return INFLATE_STREAM_ERROR;
		a_destination.context = new (std::nothrow) isalDecoderDetail::Context(
			*static_cast<const isalDecoderDetail::Context*>(a_source.context));
		return a_destination.context ? INFLATE_OK : INFLATE_MEM_ERROR;
	}

	int32_t IsalDecoder::SetDictionary(State& a_state, std::span<const uint8_t> a_dictionary) noexcept
	{
		auto* context = static_cast<isalDecoderDetail::Context*>(a_state.context);
		if (!context || a_dictionary.size() > UINT32_MAX)
			return INFLATE_STREAM_ERROR;
		if (context->stream.crc_flag != ISAL_DEFLATE)
		{
			if (!context->needsDictionary)
				return INFLATE_STREAM_ERROR;
			if (libdeflate_adler32(1, a_dictionary.data(), a_dictionary.size()) != context->stream.dict_id)
				return INFLATE_DATA_ERROR;
		}
		const auto result = isal_inflate_set_dict(&context->stream, const_cast<uint8_t*>(a_dictionary.data()),
			static_cast<uint32_t>(a_dictionary.size()));
		if (result == 0)
			context->needsDictionary = false;
		return result == 0 ? INFLATE_OK : INFLATE_STREAM_ERROR;
	}
}
