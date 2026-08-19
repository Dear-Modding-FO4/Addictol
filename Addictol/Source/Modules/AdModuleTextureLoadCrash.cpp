#include <Modules/AdModuleTextureLoadCrash.h>
#include <AdUtils.h>
#include <RE/N/NiTexture.h>
#include <RE/B/BSResourceNiBinaryStream.h>
#include <RE/B/BSGraphics.h>

#include <Windows.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesTextureLoadCrash{ "Fixes"sv, "bTextureLoadCrash"sv, true };

	static inline REL::Relocation<void(RE::BSResourceNiBinaryStream* a_self, RE::BSTSmartPointer<RE::BSResource::Stream>& a_stream,
		bool a_fullReadHint, bool a_useOwnBuffer)> BSResourceNiBinaryStream_ctorFromResourceStream{ REL::ID{ 306612, 2269831 } };
	static inline REL::Relocation<void(RE::BSGraphics::Renderer* a_self, int8_t a_loadlevel)>
		BSGraphicsRenderer_SetTextureLoadLevel{ REL::ID{ 79052, 2277268 } };
	static inline REL::Relocation<RE::BSGraphics::Texture*(RE::BSGraphics::Renderer* a_self, RE::BSResourceNiBinaryStream*,
		bool a_isDDS, bool a_isSRGB, bool a_canDegradeLevel)> BSGraphicsRenderer_CreateTextureFromStream{ REL::ID{ 39969, 2276913 } };
	static inline REL::Relocation<bool(RE::BSGraphics::Renderer* a_self, RE::BSGraphics::Texture* a_texture)>
		BSGraphicsRenderer_CanTextureDegrade{ REL::ID{ 674041, 2277274 } };
	static inline REL::Relocation<RE::BSGraphics::Renderer*> BSGraphicsRenderer{ REL::ID{ 1378294, 2704525 } };

	static void BSShaderResourceManager_LoadTexture([[maybe_unused]] void* a_sender, RE::NiTexture* a_texture) noexcept
	{
		// Fixed ctd if a_texture is nullptr
		if (!a_texture)
			return;

		// CommmonLib's constructor is a different one, so we can't use it here
		std::byte streamBytes[sizeof(RE::BSResourceNiBinaryStream)]{};
		std::fill_n(reinterpret_cast<uint8_t*>(&streamBytes), sizeof(RE::BSResourceNiBinaryStream), 0);
		auto* stream = reinterpret_cast<RE::BSResourceNiBinaryStream*>(&streamBytes);
		BSResourceNiBinaryStream_ctorFromResourceStream(stream, a_texture->stream, true, false);

		auto Renderer = reinterpret_cast<RE::BSGraphics::Renderer*>(BSGraphicsRenderer.address());
		BSGraphicsRenderer_SetTextureLoadLevel(Renderer, a_texture->desiredDegradeLevel);
		
		auto texture = BSGraphicsRenderer_CreateTextureFromStream(Renderer, stream, static_cast<bool>(a_texture->isDDX),
			static_cast<bool>(a_texture->isSRGB), static_cast<bool>(a_texture->flags & (1 << 6)));
		a_texture->rendererTexture = texture;
		if (texture)
		{
			// orig (psevdo c):
			// if ((*(_DWORD*)(a2 + 24) & 0x40) != 0 && 
			//		(unsigned __int8)BSGraphics::Renderer::CanTextureDegrade_BSGraphics::Texture__(&byte_1461E0900,
			//		TextureFromStream_BSResourceNiBinaryStream__bool_bool_bool)) {
			//			*(_DWORD*)(a2 + 24) |= 0x40u;
			// }
			// else
			//		*(_DWORD*)(a2 + 24) &= ~0x40u;
			//
			// perchik71: In fact, if one of the conditions is incorrect, the flag is disabled, which would be generally correct, 
			// but I was confused that Bethesda sets the flag if two conditions match. But why...
			// I assume that depending on the check, need to unset or set the flag.
			// I should note that there is a function that calls this code, then unset the flag, possibly trying to fix a bug.

			if (BSGraphicsRenderer_CanTextureDegrade(Renderer, texture))
				a_texture->flags |= (1 << 6);
			else
				a_texture->flags &= ~(1 << 6);

			a_texture->desiredDegradeLevel = static_cast<int8_t>(((std::byte*)texture)[0x3D]);
			BSGraphicsRenderer_SetTextureLoadLevel(Renderer, 0);
		}
		else
			// We inform you that it was not possible to create a texture, 
			// the texture will be specified as nullptr, this will not lead to ctd directly 
			// if it is not related to scaleform, namely pipboy.
			// If you open pipboy and see the defects, you will get a ctd in a couple of seconds.
			REX::WARN("Texture load failed \"{}\""sv, a_texture->name.c_str());

		stream->~BSResourceNiBinaryStream();
	}

	ModuleTextureLoadCrash::ModuleTextureLoadCrash() :
		Module("Texture Load Crash", &bFixesTextureLoadCrash)
	{}

	bool ModuleTextureLoadCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation vtable{ RE::VTABLE::BSShaderResourceManager[0] };
		vtable.write_vfunc(26, BSShaderResourceManager_LoadTexture);

		return true;
	}

}