#include <Modules/AdModuleSprintSpeedMult.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorState.h>
#include <RE/A/ActorValue.h>
#include <RE/P/PlayerCharacter.h>

#include <cmath>

namespace Addictol
{
	namespace sprintSpeedMultDetail
	{
		constexpr REL::ID s_doSetMoveMode{ 196994, 2231463, 2231463 };
		RE::ActorValueInfo* s_speedMultAVIF;

		struct DoSetMoveMode
		{
			static bool thunk(RE::ActorState* a_state, uint16_t a_flags)
			{
				const auto result = func(a_state, a_flags);
				if (a_flags & 0x100)
				{
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player && a_state == static_cast<RE::ActorState*>(player))
					{
						// Match native movement normalization; never feed a nonfinite rate to the graph.
						auto factor = std::fabs(player->GetActorValue(*s_speedMultAVIF)) * 0.01f;
						if (factor == 0.0f || !std::isfinite(factor))
						{
							factor = 1.0f;
						}

						static const RE::BSFixedString s_sprintPlaybackSpeed{ "fLocomotionSprintPlaybackSpeed"sv };
						// Result ignored: the first-person graph has no such variable.
						player->SetGraphVariableFloat(s_sprintPlaybackSpeed, factor);
					}
				}

				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleSprintSpeedMult::ModuleSprintSpeedMult() :
		Module("Sprint Speed Mult", &bFixesSprintSpeedMult)
	{}

	bool ModuleSprintSpeedMult::DoQuery() const noexcept
	{
		if (IsModDLLPresent("SprintSpeedController.dll"))
		{
			Skip("standalone 'SprintSpeedController.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("SprintSpeedControllerAE.dll"))
		{
			Skip("standalone 'SprintSpeedControllerAE.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleSprintSpeedMult::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto* actorValues = RE::ActorValue::GetSingleton();
		sprintSpeedMultDetail::s_speedMultAVIF = actorValues ? actorValues->speedMult : nullptr;
		if (!sprintSpeedMultDetail::s_speedMultAVIF)
		{
			Skip("Failed to get the SpeedMult AVIF."sv);
			return false;
		}

		const auto target = REL::Relocation<uintptr_t>{ sprintSpeedMultDetail::s_doSetMoveMode }.address();
		// Preserve upper bits at +0x08, assign the low 14 move-mode bits, then return true.
		static constexpr std::initializer_list<uint8_t> s_expected{
			0x81, 0x61, 0x08, 0x00, 0xC0, 0xFF, 0xFF,
			0x0F, 0xB7, 0xC2, 0x25, 0xFF, 0x3F, 0x00, 0x00,
			0x09, 0x41, 0x08, 0xB0, 0x01, 0xC3
		};
		if (!RELEX::Validate(target, s_expected))
		{
			Skip("ActorState::DoSetMoveMode bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		sprintSpeedMultDetail::DoSetMoveMode::func = RELEX::DetourClassJump(target, &sprintSpeedMultDetail::DoSetMoveMode::thunk);
		if (!sprintSpeedMultDetail::DoSetMoveMode::func.address())
		{
			Skip("Failed to detour ActorState::DoSetMoveMode."sv);
			return false;
		}

		return true;
	}
}
