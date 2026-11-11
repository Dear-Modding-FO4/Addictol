#include <Modules/AdModuleTextureCreateCrash.h>
#include <Core/AdUtils.h>
#include <REL/REL.h>

#include <array>

namespace Addictol
{
	namespace textureCreateCrashDetail
	{
		// First use of the argument is `mov rax, [rcx]`, the ID3D11Texture2D vtable.
		static inline REL::Relocation<void(void*)> RegisterD3DTexture{ REL::ID{ 788672, 2277287 } };

		static LogLimiter s_refusals{ 8, "Texture Create Crash: further refused textures will not be logged"sv };

		// BSD3DResourceCreator's worker never checks the HRESULT:
		//
		//     call [rax+0x28]        ; CreateTexture2D -> E_INVALIDARG, out pointer left null
		//     mov  rcx, [rsi+8]      ; the null out pointer
		//     call RegisterD3DTexture
		//     lock dec [rsi+0x30]
		//
		// So any description D3D11 refuses (a compressed texture whose top mip is not a multiple of 4, 
		// an unsupported format, a dimension of over 16384, etc) crashes the worker instead of failing the load.
		// A null texture is what the engine's own guarded sites already produce: 
		// the view request stores the null result without dereferencing it, and a null view binds legally.
		static void HKRegisterD3DTexture(void* a_texture) noexcept
		{
			if (a_texture) [[likely]]
			{
				RegisterD3DTexture(a_texture);
				return;
			}

			if (const auto report = s_refusals.Take())
				REX::WARN("Texture Create Crash: D3D11 refused a texture description, skipped its registration (#{})"sv, report);
		}

	}

	ModuleTextureCreateCrash::ModuleTextureCreateCrash() :
		Module("Texture Create Crash", &bFixesTextureCreateCrash)
	{}

	bool ModuleTextureCreateCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace textureCreateCrashDetail;

		// The calls the engine does not guard with `test rcx,rcx`, each right after a call through
		// the device's CreateTexture2D slot.
		const std::array<uintptr_t, 3> sites
		{
			REL::Relocation{ REL::ID{ 678241,	2277437 }, REL::Offset{ 0x152, 0x222 } }.address(),	// deferred create request
			REL::Relocation{ REL::ID{ 1165600,	2277444 }, REL::Offset{ 0x57 } }.address(),			// creator helper
			REL::Relocation{ REL::ID{ 367479,	2277445 }, REL::Offset{ 0xBA } }.address(),			// creator helper
		};

		// Check all of them before writing any, so an odd layout cannot half patch the renderer.
		for (const auto site : sites)
		{
			if (!RELEX::IsCallTo(site, RegisterD3DTexture.address()))
			{
				REX::ERROR("Texture Create Crash: no RegisterD3DTexture call at 0x{:X}, nothing was patched"sv, site);
				return false;
			}
		}

		for (const auto site : sites)
		{
			if (RELEX::DetourCall(site, reinterpret_cast<uintptr_t>(&HKRegisterD3DTexture)) == 0)
			{
				REX::ERROR("Texture Create Crash: failed to patch the RegisterD3DTexture call at 0x{:X}"sv, site);
				return false;
			}
		}

		return true;
	}
}
