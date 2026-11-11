#include <Modules/AdModuleTextureArraySliceCrash.h>
#include <Core/AdUtils.h>

#include <RE/B/BSGraphics.h>
#include <RE/I/IRendererResourceManager.h>
#include <RE/N/NiTexture.h>

#include <atomic>

namespace Addictol
{
	namespace textureArraySliceCrashDetail
	{
		using Texture = RE::BSGraphics::Texture;

		// Slot 0xF0 reads the array's Texture without holding a reference.
		static inline REL::Relocation<uintptr_t> ReadArraySlice{ REL::ID{ 923640, 2319764, 4505835 } };
		static inline REL::Relocation<bool(RE::BSGraphics::Renderer*, Texture*, void*, uint32_t)> ReadStreamingTextureDataToArraySlice{ REL::ID{ 9669, 2277272 } };

		static constexpr size_t s_textureRefCount{ 0x34 };

		static LogLimiter s_skipped{ 8, "Texture Array Slice Crash: further skipped slices will not be logged"sv };

		// The array can be released while the slice inflates; hold a reference.
		static bool HKReadStreamingTextureDataToArraySlice(RE::IRendererResourceManager* a_manager, RE::NiTexture* a_owner, void* a_stream, uint32_t a_slice) noexcept
		{
			const std::atomic_ref ownerTexture{ a_owner->rendererTexture };
			const auto texture = ownerTexture.load(std::memory_order_acquire);
			if (!texture)
				return false;

			std::atomic_ref refCount{ *reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(texture) + s_textureRefCount) };

			// Zero means the release is queued; reviving it would double free.
			auto count = refCount.load(std::memory_order_relaxed);
			do
			{
				if (count == 0)
				{
					if (const auto report = s_skipped.Take())
						REX::WARN("Texture Array Slice Crash: skipped a streamed slice of a released texture array (#{})"sv, report);
					return false;
				}
			} while (!refCount.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel));

			// The block may be another texture's by the time the reference lands.
			if (ownerTexture.load(std::memory_order_acquire) != texture)
			{
				a_manager->DecRefTexture(texture);
				if (const auto report = s_skipped.Take())
					REX::WARN("Texture Array Slice Crash: skipped a streamed slice of a replaced texture array (#{})"sv, report);
				return false;
			}

			const bool result = ReadStreamingTextureDataToArraySlice(RE::BSGraphics::Renderer::GetSingleton(), texture, a_stream, a_slice);
			a_manager->DecRefTexture(texture);
			return result;
		}
	}

	ModuleTextureArraySliceCrash::ModuleTextureArraySliceCrash() :
		Module("Texture Array Slice Crash", &bFixesTextureArraySliceCrash)
	{}

	bool ModuleTextureArraySliceCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace textureArraySliceCrashDetail;

		// mov rdx, [rdx+38h]; test rdx, rdx; jz +0xC; lea rcx, [rip+x]; jmp read
		const auto target = ReadArraySlice.address();
		if (!RELEX::Validate(target, { 0x48, 0x8B, 0x52, 0x38, 0x48, 0x85, 0xD2, 0x74, 0x0C, 0x48, 0x8D, 0x0D }) ||
			!RELEX::IsJumpTo(target + 0x10, ReadStreamingTextureDataToArraySlice.address()))
		{
			Skip("the array slice read does not match; unsupported runtime or another plugin patched it"sv);
			return false;
		}

		if (RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&HKReadStreamingTextureDataToArraySlice)) == 0)
		{
			REX::ERROR("Texture Array Slice Crash: failed to patch the array slice read at 0x{:X}"sv, target);
			return false;
		}

		return true;
	}
}
