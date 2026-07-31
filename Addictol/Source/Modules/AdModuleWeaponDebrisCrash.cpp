#include <Modules/AdModuleWeaponDebrisCrash.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWeaponDebrisCrash{ "Fixes"sv, "bWeaponDebrisCrash"sv, true };

	ModuleWeaponDebrisCrash::ModuleWeaponDebrisCrash() :
		Module("Weapon Debris Crash", &bFixesWeaponDebrisCrash)
	{}

	bool ModuleWeaponDebrisCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleWeaponDebrisCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Force the FleX collision-geometry ja unconditional to skip the Turing+ CTD path.
		const REL::Relocation<std::uintptr_t> func{ REL::ID{ 22388, 2195766 } };
		const auto src = func.address() + REL::Offset{ 0x52, 0x4F }.offset();
		const auto dst = func.address() + REL::Offset{ 0x703, 0x6DD }.offset();

		const auto* const p = reinterpret_cast<const std::uint8_t*>(src);
		if (p[0] != 0x0F || p[1] != 0x87)
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

		const auto rel = static_cast<std::int32_t>(dst - (src + 5));
		const auto* const r = reinterpret_cast<const std::uint8_t*>(&rel);
		RELEX::WriteSafe(src, { 0xE9, r[0], r[1], r[2], r[3], 0x90 });
		return true;
	}

	bool ModuleWeaponDebrisCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleWeaponDebrisCrash::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
