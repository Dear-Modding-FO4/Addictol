#include <Modules/AdModuleEffectEnvMapCrash.h>
#include <Core/AdUtils.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	// The env slot nulls its texture bind, then reads that null texture's min LOD.
	namespace effectEnvMapCrashDetail
	{
		struct NullGuard : RELEX::Stub
		{
			NullGuard(bool a_og, uintptr_t a_resume, uintptr_t a_skip)
			{
				// OG reads it from r11 in Renderer::SetTexture; NG/AE inline it on r8.
				const auto& texture = a_og ? r11 : r8;
				const auto& lod = a_og ? eax : ecx;
				Xbyak::Label noTexture;

				test(texture, texture);
				jz(noTexture);
				movzx(lod, byte[texture + 0x3C]);
				JumpTo(a_resume);
				L(noTexture);
				JumpTo(a_skip);
			}
		};
	}

	ModuleEffectEnvMapCrash::ModuleEffectEnvMapCrash() :
		Module("Effect Env Map Crash", &bFixesEffectEnvMapCrash)
	{}

	bool ModuleEffectEnvMapCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace effectEnvMapCrashDetail;

		uintptr_t site = 0;
		size_t skipOffset = 0;
		bool valid = false;

		if (RELEX::IsRuntimeOG())
		{
			site = REL::Relocation<uintptr_t>{ RE::ID::BSGraphics::Renderer::SetTexture, 0x6D }.address();
			skipOffset = 0x27;
			// movzx eax, byte [r11+3Ch] through or [rdx+18h], r8d
			valid = RELEX::Validate(site, { 0x41, 0x0F, 0xB6, 0x43, 0x3C, 0x41, 0x3B, 0xC1, 0x44, 0x0F, 0x47, 0xC8,
				0x44, 0x39, 0x8C, 0x9A, 0x60, 0x04, 0x00, 0x00, 0x74, 0x11, 0x8B, 0xCB, 0x44, 0x89, 0x8C, 0x9A,
				0x60, 0x04, 0x00, 0x00, 0x41, 0xD3, 0xE0, 0x44, 0x09, 0x42, 0x18 });
		}
		else
		{
			site = REL::Relocation<uintptr_t>{ RE::ID::BSEffectShader::SetupMaterial, 0x619 }.address();
			skipOffset = 0x24;
			// movzx ecx, byte [r8+3Ch] through or dword [rdx+18h], 20h
			valid = RELEX::Validate(site, { 0x41, 0x0F, 0xB6, 0x48, 0x3C, 0x41, 0x3A, 0xCA, 0x41, 0x0F, 0xB6, 0xC2,
				0x0F, 0x46, 0xC8, 0x0F, 0xB6, 0xC1, 0x39, 0x82, 0x74, 0x04, 0x00, 0x00, 0x74, 0x0A, 0x89, 0x82,
				0x74, 0x04, 0x00, 0x00, 0x83, 0x4A, 0x18, 0x20 });
		}

		if (!valid)
		{
			Skip("env-map min-LOD site bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		NullGuard guard{ RELEX::IsRuntimeOG(), site + 5, site + skipOffset };
		guard.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(site, trampoline.allocate(guard));

		return true;
	}
}
