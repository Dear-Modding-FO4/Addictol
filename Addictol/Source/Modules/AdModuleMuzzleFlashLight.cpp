#include <Modules/AdModuleMuzzleFlashLight.h>
#include <AdUtils.h>

#include <RE/N/NiAVObject.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMuzzleFlashLight{ "Fixes"sv, "bMuzzleFlashLight"sv, true };

	using TUpdateLight = void(__fastcall*)(void*, bool);
	static TUpdateLight OriginalUpdateLight = nullptr;

	// MuzzleFlash layout (RE'd from OG/AE disasm): bEnabled @ +0x00, pLight @ +0x20.
	static void __fastcall HookUpdateLight(void* a_this, bool a_alive) noexcept
	{
		auto* p = reinterpret_cast<std::byte*>(a_this);
		if (auto* light = *reinterpret_cast<RE::NiAVObject* const*>(p + 0x20))
		{
			const bool enabled = *reinterpret_cast<const bool*>(p);
			light->SetAppCulled(!enabled);
		}

		if (OriginalUpdateLight)
			OriginalUpdateLight(a_this, a_alive);
	}

	ModuleMuzzleFlashLight::ModuleMuzzleFlashLight() :
		Module("Muzzle Flash Light", &bFixesMuzzleFlashLight)
	{}

	bool ModuleMuzzleFlashLight::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::ID{ 976501, 2236921 }.address();
		*reinterpret_cast<uintptr_t*>(&OriginalUpdateLight) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&HookUpdateLight));
		return OriginalUpdateLight != nullptr;
	}

}
