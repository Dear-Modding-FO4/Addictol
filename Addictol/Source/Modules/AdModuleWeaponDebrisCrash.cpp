#include <Modules/AdModuleWeaponDebrisCrash.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWeaponDebrisCrash{ "Fixes"sv, "bWeaponDebrisCrash"sv, true };

	ModuleWeaponDebrisCrash::ModuleWeaponDebrisCrash() :
		Module("Weapon Debris Crash", &bFixesWeaponDebrisCrash)
	{}

	bool ModuleWeaponDebrisCrash::DoQuery() const noexcept
	{
		if (IsModDLLPresent("WeaponDebrisCrashFix.dll"))
		{
			Skip("Standalone 'WeaponDebrisCrashFix.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleWeaponDebrisCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const REL::Relocation func{ REL::ID{ 22388, 2195766 } };
		const auto src = func.address() + REL::Offset{ 0x52, 0x4F }.offset();
		const auto dst = func.address() + REL::Offset{ 0x703, 0x6DD }.offset();

		if (!RELEX::Validate(src, { 0x0F, 0x87 }))
		{
			REX::WARN("Weapon Debris Crash: branch site is not `ja rel32` -- skipping to avoid corruption."sv);
			return false;
		}

		const auto jaTarget = src + 6 + static_cast<std::uintptr_t>(*reinterpret_cast<const std::int32_t*>(src + 2));
		if (jaTarget != dst)
		{
			REX::WARN("Weapon Debris Crash: branch target != expected skip address -- skipping to avoid corruption."sv);
			return false;
		}

		// Force the FleX collision-geometry ja unconditional to skip the Turing+ CTD path.
		const auto rel = static_cast<std::int32_t>(dst - (src + 5));
		const auto* const r = reinterpret_cast<const std::uint8_t*>(&rel);
		RELEX::WriteSafe(src, { 0xE9, r[0], r[1], r[2], r[3], 0x90 });
		return true;
	}

}
