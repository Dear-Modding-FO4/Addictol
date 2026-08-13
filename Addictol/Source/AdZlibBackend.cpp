#include <AdZlibBackend.h>

#include <libdeflate/libdeflate.h>
#include <REX/REX.h>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		ZlibBackendKind s_selectedBackend = DEFAULT_ZLIB_BACKEND;

		libdeflate_decompressor*& GetThreadDecompressor() noexcept
		{
			thread_local libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
			return decompressor;
		}
	}

	ZlibBackendKind ResolveZlibBackendSelection(std::string_view a_name) noexcept
	{
		if (const auto backend = ParseZlibBackend(a_name))
			s_selectedBackend = *backend;
		else
		{
			s_selectedBackend = DEFAULT_ZLIB_BACKEND;
			REX::WARN(
				"Unknown or unavailable zlib backend \"{}\"; valid values are: stock, libdeflate; using libdeflate."sv,
				a_name);
		}

		return s_selectedBackend;
	}

	ZlibBackendKind GetSelectedZlibBackendKind() noexcept
	{
		return s_selectedBackend;
	}

	bool LibDeflateZlibBackend::Prepare() noexcept
	{
		return GetThreadDecompressor() != nullptr;
	}

	ZlibDecodeResult LibDeflateZlibBackend::Decode(
		std::span<const std::uint8_t> a_input,
		std::span<std::uint8_t> a_output) noexcept
	{
		std::size_t consumed = 0;
		std::size_t produced = 0;
		const auto result = libdeflate_zlib_decompress_ex(
			GetThreadDecompressor(),
			a_input.data(),
			a_input.size(),
			a_output.data(),
			a_output.size(),
			&consumed,
			&produced);
		if (result != LIBDEFLATE_SUCCESS)
			return { ZlibDecodeStatus::Failed };

		return { ZlibDecodeStatus::Success, consumed, produced };
	}
}
