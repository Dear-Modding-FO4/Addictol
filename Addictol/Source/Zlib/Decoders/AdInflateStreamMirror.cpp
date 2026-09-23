#include <Zlib/Decoders/AdInflateStreamMirror.h>
#include <libdeflate/libdeflate.h>

namespace Addictol
{
	void InflateStreamMirror::Reset(int32_t a_windowBits) noexcept
	{
		m_held = 0;
		m_headerSize = 0;
		m_auto = a_windowBits >= 32;
		m_gzip = a_windowBits >= 16 && a_windowBits < 32;
		m_raw = a_windowBits < 0;
		m_checksum = m_gzip || m_raw ? 0 : 1;
	}

	void InflateStreamMirror::ObserveHeader(std::span<const uint8_t> a_input) noexcept
	{
		if (!m_auto)
			return;
		for (auto byte : a_input)
		{
			m_header[m_headerSize++] = byte;
			if (m_headerSize == 2)
			{
				m_gzip = m_header[0] == 0x1F && m_header[1] == 0x8B;
				m_checksum = m_gzip ? 0 : 1;
				m_auto = false;
				break;
			}
		}
	}

	void InflateStreamMirror::UpdateChecksum(std::span<const uint8_t> a_output) noexcept
	{
		if (!m_raw && !a_output.empty())
			m_checksum = m_gzip ? libdeflate_crc32(m_checksum, a_output.data(), a_output.size()) :
				libdeflate_adler32(m_checksum, a_output.data(), a_output.size());
	}
}
