#pragma once

#include <Zlib/AdZlibBackend.h>
#include <concepts>

namespace Addictol
{
	inline constexpr int32_t INFLATE_OK = 0;
	inline constexpr int32_t INFLATE_END = 1;
	inline constexpr int32_t INFLATE_NEED_DICT = 2;
	inline constexpr int32_t INFLATE_STREAM_ERROR = -2;
	inline constexpr int32_t INFLATE_DATA_ERROR = -3;
	inline constexpr int32_t INFLATE_MEM_ERROR = -4;
	inline constexpr int32_t INFLATE_BUF_ERROR = -5;
	inline constexpr int32_t INFLATE_VERSION_ERROR = -6;

	struct InflateCapabilities
	{
		bool copy{}, setDictionary{}, sync{}, prime{}, mark{}, getHeader{}, undermine{};
		bool resetKeep{}, syncPoint{};
	};

	struct InflateHeader
	{
		int32_t text{};
		uint32_t time{};
		int32_t xflags{}, os{};
		uint8_t* extra{};
		uint32_t extra_len{}, extra_max{};
		uint8_t* name{};
		uint32_t name_max{};
		uint8_t* comment{};
		uint32_t comm_max{};
		int32_t hcrc{}, done{};
	};

	template<class T>
	concept WholeInflateDecoder = requires(std::span<const uint8_t> a_input, std::span<uint8_t> a_output)
	{
		{ T::Decode(a_input, a_output) } noexcept -> std::same_as<ZlibDecodeResult>;
	};

	template<class T>
	concept StreamingInflateDecoder = requires(typename T::State& a_state, ZlibInflate::Stream& a_stream)
	{
		{ T::capabilities } -> std::convertible_to<InflateCapabilities>;
		{ T::Init(a_state, 15) } noexcept -> std::same_as<int32_t>;
		{ T::Inflate(a_state, a_stream, 0) } noexcept -> std::same_as<int32_t>;
		{ T::Reset(a_state) } noexcept -> std::same_as<int32_t>;
		{ T::Reset2(a_state, 15) } noexcept -> std::same_as<int32_t>;
		{ T::End(a_state) } noexcept -> std::same_as<int32_t>;
		{ T::HasPendingOutput(a_state) } noexcept -> std::same_as<bool>;
	} &&
		(!T::capabilities.copy || requires(typename T::State& a, const typename T::State& b) { { T::Copy(a, b) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.setDictionary || requires(typename T::State& a, std::span<const uint8_t> b) { { T::SetDictionary(a, b) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.sync || requires(typename T::State& a, ZlibInflate::Stream& b) { { T::Sync(a, b) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.prime || requires(typename T::State& a) { { T::Prime(a, 0, 0) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.mark || requires(const typename T::State& a) { { T::Mark(a) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.getHeader || requires(typename T::State& a, InflateHeader& b) { { T::GetHeader(a, b) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.undermine || requires(typename T::State& a) { { T::Undermine(a, 0) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.resetKeep || requires(typename T::State& a) { { T::ResetKeep(a) } noexcept -> std::same_as<int32_t>; }) &&
		(!T::capabilities.syncPoint || requires(const typename T::State& a) { { T::SyncPoint(a) } noexcept -> std::same_as<int32_t>; });

	struct NoWholeInflateDecoder {};
}
