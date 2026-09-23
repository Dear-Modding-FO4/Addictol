#pragma once

#include <Zlib/Decoders/AdInflateDecoder.h>

namespace Addictol
{
	template<class Native, class Function>
	int32_t InvokeNativeInflate(Native& a_native, ZlibInflate::Stream& a_view, Function&& a_function) noexcept
	{
		a_native.next_in = const_cast<uint8_t*>(a_view.next_in);
		a_native.avail_in = a_view.avail_in;
		a_native.next_out = a_view.next_out;
		a_native.avail_out = a_view.avail_out;
		a_native.total_out = a_view.total_out;
		if constexpr (requires { a_native.total_in; })
			a_native.total_in = a_view.total_in;
		const auto inputBefore = a_view.avail_in;
		const auto result = a_function();
		a_view.next_in = a_native.next_in;
		a_view.avail_in = a_native.avail_in;
		a_view.next_out = a_native.next_out;
		a_view.avail_out = a_native.avail_out;
		a_view.total_out = static_cast<uint32_t>(a_native.total_out);
		if constexpr (requires { a_native.total_in; })
			a_view.total_in = static_cast<uint32_t>(a_native.total_in);
		else
			a_view.total_in += inputBefore - a_view.avail_in;
		if constexpr (requires { a_native.msg; a_native.adler; a_native.data_type; })
		{
			a_view.msg = a_native.msg;
			a_view.adler = static_cast<uint32_t>(a_native.adler);
			a_view.data_type = a_native.data_type;
		}
		return result;
	}

	class InflateStreamMirror
	{
	public:
		void Reset(int32_t a_windowBits) noexcept;
		uint32_t InitialChecksum() const noexcept { return m_checksum; }
		void AfterSync(uint32_t a_checksum) noexcept { m_raw = true; m_auto = false; m_checksum = a_checksum; }

		template<class Function, class Pending>
		int32_t Invoke(ZlibInflate::Stream& a_stream, Function&& a_function, Pending&& a_pending, bool a_requiresOutput = true) noexcept
		{
			if ((a_requiresOutput && !a_stream.next_out) || (a_stream.avail_in && !a_stream.next_in) || a_stream.avail_in < m_held)
				return INFLATE_STREAM_ERROR;
			auto view = a_stream;
			if (m_held)
			{
				view.next_in += m_held;
				view.avail_in -= m_held;
				view.total_in += m_held;
			}
			const auto* input = view.next_in;
			auto* output = view.next_out;
			const auto outputBefore = view.avail_out;
			const auto inputBefore = view.avail_in;
			const auto result = a_function(view);
			if (view.avail_in < inputBefore)
				ObserveHeader({ input, inputBefore - view.avail_in });
			UpdateChecksum({ output, outputBefore - view.avail_out });
			if (result != INFLATE_NEED_DICT)
				view.adler = m_checksum;
			// Retain one input byte until pending output is delivered, but release it when more input is needed.
			const bool hold = (result == INFLATE_OK || result == INFLATE_BUF_ERROR) &&
				view.avail_out == 0 && view.avail_in == 0 && (inputBefore != 0 || m_held != 0) && a_pending();
			m_held = hold ? 1u : 0u;
			if (hold)
			{
				--view.next_in;
				++view.avail_in;
				--view.total_in;
			}
			a_stream.next_in = view.next_in;
			a_stream.avail_in = view.avail_in;
			a_stream.total_in = view.total_in;
			a_stream.next_out = view.next_out;
			a_stream.avail_out = view.avail_out;
			a_stream.total_out = view.total_out;
			a_stream.msg = view.msg;
			a_stream.adler = view.adler;
			a_stream.data_type = view.data_type;
			return result;
		}

	private:
		void ObserveHeader(std::span<const uint8_t>) noexcept;
		void UpdateChecksum(std::span<const uint8_t>) noexcept;
		uint32_t m_held{}, m_checksum{};
		uint8_t m_header[2]{};
		uint8_t m_headerSize{};
		bool m_auto{}, m_gzip{}, m_raw{};
	};
}
