// port: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/archery_downward_aiming.h

#include <Modules/AdModuleDownwardAiming.h>
#include <Core/AdUtils.h>

#include <RE/A/AIProcess.h>
#include <RE/A/Actor.h>
#include <RE/C/CachedValues.h>
#include <RE/N/NiPoint3.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/Projectile.h>

namespace Addictol
{
	namespace detail
	{
		// GetEyeVector's "cachedEyeLevel is valid" bit in CachedValues::flags.
		inline constexpr uint32_t kCachedEyeLevelFlag = 1u << 9;

		struct Projectile
		{
			static void Move(RE::Projectile* a_self, RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
			{
				const auto refShooter = a_self->shooter.get();
				if (refShooter && refShooter->Is(RE::Actor::FORM_ID))
				{
					const auto akShooter = static_cast<RE::Actor*>(refShooter.get());  // NOLINT(*-pro-type-static-cast-downcast)
					if (akShooter == RE::PlayerCharacter::GetSingleton())
					{
						[[maybe_unused]] RE::NiPoint3 direction;
						akShooter->GetEyeVector(a_from, direction, true);
					}
					else if (const auto* const process = akShooter->currentProcess)
					{
						// Parallel projectile jobs must not build eye bones; use the cache only.
						if (const auto* const cached = process->cachedValues;
							cached && (cached->flags & kCachedEyeLevelFlag) != 0)
						{
							a_from = akShooter->data.location;
							a_from.z += cached->cachedEyeLevel;
						}
					}
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
		detail::Projectile::_Move = (decltype(detail::Projectile::Move)*)REL::Relocation{ RE::ID::Projectile::Move }.get();
		return RELEX::DetourCall(REL::Relocation{ RE::ID::MissileProjectile::UpdateImpl, REL::Offset{ 0x975, 0xCD9 } }.address(),
			reinterpret_cast<uintptr_t>(&detail::Projectile::Move)) != 0;
	}
}