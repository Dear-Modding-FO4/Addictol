#include <Zlib/AdZlibBackend.h>

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
		std::span<const uint8_t> a_input,
		std::span<uint8_t> a_output) noexcept
	{
		const auto result = DecodeExact(a_input, a_output);
		const auto status = result.codecResult == ZLIB_CODEC_SUCCESS ? ZlibDecodeStatus::Success :
			result.codecResult == ZLIB_CODEC_INSUFFICIENT_SPACE ? ZlibDecodeStatus::InsufficientSpace :
			ZlibDecodeStatus::BadData;
		return { status, result.consumed, result.produced, result.codecResult };
	}

	ZlibExactDecode LibDeflateZlibBackend::DecodeExact(
		std::span<const uint8_t> a_input,
		std::span<uint8_t> a_output) noexcept
	{
		static_assert(ZLIB_CODEC_SUCCESS == LIBDEFLATE_SUCCESS);
		static_assert(ZLIB_CODEC_BAD_DATA == LIBDEFLATE_BAD_DATA);
		static_assert(ZLIB_CODEC_SHORT_OUTPUT == LIBDEFLATE_SHORT_OUTPUT);
		static_assert(ZLIB_CODEC_INSUFFICIENT_SPACE == LIBDEFLATE_INSUFFICIENT_SPACE);

		auto* decompressor = GetThreadDecompressor();
		if (!decompressor)
			return {};

		ZlibExactDecode decode{};
		decode.codecResult = static_cast<uint32_t>(libdeflate_zlib_decompress_ex(
			decompressor,
			a_input.data(),
			a_input.size(),
			a_output.data(),
			a_output.size(),
			&decode.consumed,
			&decode.produced));
		return decode;
	}
}
