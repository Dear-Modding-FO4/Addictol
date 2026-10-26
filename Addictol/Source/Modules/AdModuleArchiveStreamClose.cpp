#include <Modules/AdModuleArchiveStreamClose.h>
#include <Core/AdUtils.h>
#include <RE/B/BSResource_Stream.h>
#include <REL/REL.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace Addictol
{
	namespace archiveStreamCloseDetail
	{
		struct Layout
		{
			ptrdiff_t sourceOffset;
			ptrdiff_t contextOffset;
			ptrdiff_t flagsOffset;
			uint32_t compressedStandardFlag;
			uint32_t multiplexedFlag;
		};

		inline constexpr Layout kLayoutOGNG{ 0x10, 0x18, 0x3C, 0x100u, 0x1000u };
		inline constexpr Layout kLayoutAE{ 0x18, 0x20, 0x44, 0x400u, 0x4000u };

		inline constexpr ptrdiff_t kContextCountersOffset = 0x80;
		inline constexpr ptrdiff_t kContextTargetsOffset = 0x88;
		inline constexpr size_t kMultiplexBufferCount = 2;

		static inline const Layout* s_layout{ &kLayoutOGNG };
		static inline void (*s_doClose)(void*){ nullptr };

		// Drain streamer reads into the context before DoClose frees it.
		static void HKDoClose(void* a_stream) noexcept
		{
			const auto* const base = static_cast<std::byte*>(a_stream);
			const auto flags = *reinterpret_cast<const uint32_t*>(base + s_layout->flagsOffset);
			const auto mask = s_layout->compressedStandardFlag | s_layout->multiplexedFlag;

			if ((flags & mask) == mask)
			{
				auto* const source = *reinterpret_cast<RE::BSResource::Stream* const*>(base + s_layout->sourceOffset);
				auto* const context = *reinterpret_cast<std::byte* const*>(base + s_layout->contextOffset);
				if (source && context)
				{
					auto* const counters = reinterpret_cast<volatile uint32_t*>(context + kContextCountersOffset);
					const auto* const targets = reinterpret_cast<const uint32_t*>(context + kContextTargetsOffset);
					for (size_t i = 0; i < kMultiplexBufferCount; ++i)
						source->DoWaitTags(&counters[i], targets[i], nullptr);
				}
			}

			s_doClose(a_stream);
		}

		[[nodiscard]] static bool ValidateWaitSite(const Layout& a_layout) noexcept
		{
			const auto sourceDisp = static_cast<uint8_t>(a_layout.sourceOffset);
			if (RELEX::IsRuntimeOG())
			{
				const auto standardRead = REL::ID{ 1183873 }.address();
				const auto updateInput = REL::ID{ 1546880 }.address();
				return RELEX::Validate(standardRead + 0x40,
						   { 0x48, 0x8B, 0x4B, static_cast<uint8_t>(a_layout.contextOffset), 0x83, 0xB9, 0x98, 0x00, 0x00, 0x00, 0x00 }) &&
					   RELEX::Validate(standardRead + 0x51,
						   { 0x8B, 0x43, static_cast<uint8_t>(a_layout.flagsOffset), 0x4C, 0x8B, 0x43, 0x20, 0x4C, 0x8D, 0x4B, 0x30, 0x25, 0x00, 0x10, 0x00, 0x00, 0x48, 0x8D, 0x53, sourceDisp }) &&
					   RELEX::Validate(updateInput + 0xED,
						   { 0x8B, 0x83, 0xA8, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x0E, 0x4C, 0x8B, 0x4B, 0x78, 0x4C, 0x8B, 0x11,
							 0x44, 0x8B, 0x84, 0x83, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x93, 0x80, 0x00, 0x00, 0x00, 0x48,
							 0x8D, 0x14, 0x82, 0x41, 0xFF, 0x52, 0x68 });
			}

			const auto standardRead = REL::ID{ 2269291 }.address();
			return RELEX::Validate(standardRead + 0x11D,
				{ 0x8B, 0x83, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x93, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4E,
				  sourceDisp, 0x4C, 0x8B, 0x4B, 0x78, 0x44, 0x8B, 0x84, 0x83, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8D,
				  0x14, 0x82, 0x4C, 0x8B, 0x11, 0x41, 0xFF, 0x52, 0x68 });
		}
	}

	ModuleArchiveStreamClose::ModuleArchiveStreamClose() :
		Module("Archive Stream Close", &bFixesArchiveStreamClose)
	{}

	bool ModuleArchiveStreamClose::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg)
			return false;

		using namespace archiveStreamCloseDetail;

		const auto target = REL::Relocation{ RE::ID::BSResource_Archive2_ReaderStream::DoClose }.address();
		s_layout = RELEX::IsRuntimeAE() ? &kLayoutAE : &kLayoutOGNG;

		const auto flagsDisp = static_cast<uint8_t>(s_layout->flagsOffset);
		const auto compressedBit = static_cast<uint8_t>(std::countr_zero(s_layout->compressedStandardFlag));

		if (!ValidateWaitSite(*s_layout))
		{
			REX::ERROR("Archive Stream Close: unexpected StandardRead/UpdateInput layout, nothing was patched"sv);
			return false;
		}

		s_doClose = reinterpret_cast<decltype(s_doClose)>(RELEX::TryDetourJump(
			target,
			reinterpret_cast<uintptr_t>(&HKDoClose),
			{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, flagsDisp, 0x48, 0x8B, 0xD9, 0x0F, 0xBA, 0xE0, compressedBit }));

		return s_doClose != nullptr;
	}
}
