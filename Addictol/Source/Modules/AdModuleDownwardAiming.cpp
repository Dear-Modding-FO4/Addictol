// port: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/archery_downward_aiming.h

#include <Modules/AdModuleDownwardAiming.h>
#include <AdUtils.h>

#include <RE/N/NiPoint3.h>
#include <RE/P/Projectile.h>
#include <RE/A/Actor.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesDownwardAiming{ "Fixes"sv, "bDownwardAiming"sv, true };

	namespace detail
	{
		struct Projectile
		{
			static void Move(RE::Projectile* a_self, RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
			{
				const auto refShooter = a_self->shooter.get();
				if (refShooter && refShooter->Is(RE::Actor::FORM_ID))
				{
					const auto akShooter = static_cast<RE::Actor*>(refShooter.get());  // NOLINT(*-pro-type-static-cast-downcast)
					[[maybe_unused]] RE::NiPoint3 direction;
					akShooter->GetEyeVector(a_from, direction, true);
				}

				_Move(a_self, a_from, a_to);
			}

			static inline decltype(Move)* _Move;
		};
	}

	ModuleDownwardAiming::ModuleDownwardAiming() :
		Module("Downward Aiming", &bFixesDownwardAiming)
	{}

	bool ModuleDownwardAiming::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg) return false;
		// called .221: rva+E3B5D0 - begin, rva+E4BD60 - after (moving bullet)
		detail::Projectile::_Move = (decltype(detail::Projectile::Move)*)REL::Relocation{ REL::ID{ 627398, 2237050 } }.get();
		return RELEX::DetourCall(REL::Relocation{ REL::ID{ 1470408, 2236880 }, REL::Offset{ 0x975, 0xCD9 } }.address(),
			reinterpret_cast<uintptr_t>(&detail::Projectile::Move)) != 0;
	}

}