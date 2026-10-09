#include <Modules/AdModuleThirdPersonSpeedMult.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorValue.h>
#include <RE/B/BSAnimationGraphManager.h>
#include <RE/H/hkStringPtr.h>
#include <RE/P/PlayerCharacter.h>

namespace Addictol
{
	namespace thirdPersonSpeedMultDetail
	{
		constexpr REL::ID s_setSneaking{ 400155, 2230404 };
		constexpr REL::ID s_doSetMoveMode{ 196994, 2231463 };
		constexpr REL::ID s_clipGeneratorUpdate{ 935260, 2261061 };

		// BShkbAnimationGraph embeds its hkbCharacter and stores the driven 3D root.
		constexpr size_t s_graphCharacter = 0x1C8;
		constexpr size_t s_graphTarget = 0x388;
		constexpr size_t s_clipName = 0x38;
		constexpr size_t s_clipPlaybackSpeed = 0xB0;

		// Third-person clips whose root motion carries the player while sprinting or sneaking.
		constexpr std::string_view s_locomotionClips[]{
			// Normal Sprint
			"SprintForward"sv, "SprintLeanLeft"sv, "SprintLeanRight"sv,
			"SprintForward00"sv, "SprintLeanLeft00"sv, "SprintLeanRight00"sv,

			// Weapon Sprint
			"WPNSprint"sv, "WPNSprint_Jump"sv,

			// Normal Sneak Walk
			"SneakWalkForward"sv, "SneakWalkForwardSlow"sv,
			"SneakWalkLeanLeft"sv, "SneakWalkLeanLeftSlow"sv,
			"SneakWalkLeanRight"sv, "SneakWalkLeanRightSlow"sv,
			"SneakWalkForward00"sv, "SneakWalkForwardSlow00"sv,
			"SneakWalkLeanLeft00"sv, "SneakWalkLeanLeftSlow00"sv,
			"SneakWalkLeanRight00"sv, "SneakWalkLeanRightSlow00"sv,

			// Normal Sneak Run
			"SneakRunForward"sv, "SneakRunLeanLeft"sv, "SneakRunLeanRight"sv,
			"SneakRunForward00"sv, "SneakRunLeanLeft00"sv, "SneakRunLeanRight00"sv,

			// Weapon Sneak Walk
			"SneakWPNWalkForwardReady"sv, "SneakWPNWalkForwardLeftReady"sv, "SneakWPNWalkForwardRightReady"sv,
			"SneakWPNWalkLeftReady"sv, "SneakWPNWalkLeftReady_Back"sv,
			"SneakWPNWalkRightReady"sv, "SneakWPNWalkRightReady_Back"sv,
			"SneakWPNWalkBackwardReady"sv, "SneakWPNWalkBackwardLeftReady"sv, "SneakWPNWalkBackwardRightReady"sv,
			"SneakWPNWalkForwardReady00"sv, "SneakWPNWalkForwardLeftReady00"sv, "SneakWPNWalkForwardRightReady00"sv,
			"SneakWPNWalkLeftReady00"sv, "SneakWPNWalkLeftReady_Back00"sv,
			"SneakWPNWalkRightReady00"sv, "SneakWPNWalkRightReady_Back00"sv,
			"SneakWPNWalkBackwardReady00"sv, "SneakWPNWalkBackwardLeftReady00"sv, "SneakWPNWalkBackwardRightReady00"sv,

			// Weapon Sneak Run
			"SneakWPNRunForwardReady"sv, "SneakWPNRunForwardLeftReady"sv, "SneakWPNRunForwardRightReady"sv,
			"SneakWPNRunLeftReady"sv, "SneakWPNRunLeftReady_Back"sv,
			"SneakWPNRunRightReady"sv, "SneakWPNRunRightReady_Back"sv,
			"SneakWPNRunBackpedalReady"sv, "SneakWPNRunBackpedalLeftReady"sv, "SneakWPNRunBackpedalRightReady"sv,
			"SneakWPNRunForwardReady00"sv, "SneakWPNRunForwardLeftReady00"sv, "SneakWPNRunForwardRightReady00"sv,
			"SneakWPNRunLeftReady00"sv, "SneakWPNRunLeftReady_Back00"sv,
			"SneakWPNRunRightReady00"sv, "SneakWPNRunRightReady_Back00"sv,
			"SneakWPNRunBackpedalReady00"sv, "SneakWPNRunBackpedalLeftReady00"sv, "SneakWPNRunBackpedalRightReady00"sv,
		};

		RE::ActorValueInfo* s_speedMultAVIF;
		std::atomic<uintptr_t> s_playerCharacter;
		std::atomic<float> s_sprintFactor{ 1.0f };

		[[nodiscard]] static float GetSpeedMultFactor(RE::PlayerCharacter* a_player)
		{
			// Match native normalization; never feed a nonfinite rate to the graph.
			const auto factor = std::fabs(a_player->GetActorValue(*s_speedMultAVIF)) * 0.01f;
			return factor == 0.0f || !std::isfinite(factor) ? 1.0f : factor;
		}

		[[nodiscard]] static uintptr_t FindThirdPersonCharacter(RE::PlayerCharacter* a_player)
		{
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			const auto* root = a_player->Get3D(false);
			if (!root || !a_player->GetAnimationGraphManagerImpl(manager) || !manager)
				return 0;

			for (const auto& graph : manager->graph)
			{
				const auto address = reinterpret_cast<uintptr_t>(graph.get());
				if (address && *reinterpret_cast<RE::NiAVObject**>(address + s_graphTarget) == root)
					return address + s_graphCharacter;
			}

			return 0;
		}

		[[nodiscard]] static bool IsLocomotionClip(uintptr_t a_clip)
		{
			const auto* name = reinterpret_cast<const RE::hkStringPtr*>(a_clip + s_clipName)->c_str();
			return name && std::ranges::find(s_locomotionClips, std::string_view{ name }) != std::end(s_locomotionClips);
		}

		static void SetSpeedMult(RE::Actor* a_actor)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_actor != player)
				return;

			const auto* state = static_cast<RE::ActorState*>(player);
			if ((state->moveMode & 0x100) == 0 && state->stance != 1)
			{
				// Cache only while sprinting or sneaking; a rebuilt graph may reuse the old address.
				s_playerCharacter.store(0, std::memory_order_relaxed);
				return;
			}

			const auto factor = GetSpeedMultFactor(player);
			s_sprintFactor.store(factor, std::memory_order_relaxed);
			s_playerCharacter.store(FindThirdPersonCharacter(player), std::memory_order_relaxed);

			// Behavior mods may bind their own clips to these variables.
			static const RE::BSFixedString s_sprintPlaybackSpeed{ "fLocomotionSprintPlaybackSpeed"sv };
			static const RE::BSFixedString s_sneakWalkPlaybackSpeed{ "fLocomotionSneakWalkPlaybackSpeed"sv };
			static const RE::BSFixedString s_sneakRunPlaybackSpeed{ "fLocomotionSneakRunPlaybackSpeed"sv };
			player->SetGraphVariableFloat(s_sprintPlaybackSpeed, factor);
			player->SetGraphVariableFloat(s_sneakWalkPlaybackSpeed, factor);
			player->SetGraphVariableFloat(s_sneakRunPlaybackSpeed, factor);
		}

		struct DoSetMoveMode
		{
			static bool thunk(RE::ActorState* a_state, uint16_t a_flags)
			{
				const auto result = func(a_state, a_flags);
				SetSpeedMult(static_cast<RE::Actor*>(a_state));
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SetSneaking
		{
			static bool thunk(RE::Actor* a_actor, bool a_sneaking)
			{
				const auto result = func(a_actor, a_sneaking);
				SetSpeedMult(a_actor);
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ClipGeneratorUpdate
		{
			// Runs on animation worker threads; reads only atomics and the clip itself.
			static void thunk(uintptr_t a_clip, uintptr_t a_context, float a_timestep)
			{
				const auto character = *reinterpret_cast<uintptr_t*>(a_context);
				if (character && character == s_playerCharacter.load(std::memory_order_relaxed) && IsLocomotionClip(a_clip))
				{
					// Same effect as a graph binding: the member feeds update and sync this frame.
					*reinterpret_cast<float*>(a_clip + s_clipPlaybackSpeed) = s_sprintFactor.load(std::memory_order_relaxed);
				}

				func(a_clip, a_context, a_timestep);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleThirdPersonSpeedMult::ModuleThirdPersonSpeedMult() :
		Module("Third Person Speed Mult", &bFixesThirdPersonSpeedMult)
	{}

	bool ModuleThirdPersonSpeedMult::DoQuery() const noexcept
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

	bool ModuleThirdPersonSpeedMult::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto* actorValues = RE::ActorValue::GetSingleton();
		thirdPersonSpeedMultDetail::s_speedMultAVIF = actorValues ? actorValues->speedMult : nullptr;
		if (!thirdPersonSpeedMultDetail::s_speedMultAVIF)
		{
			Skip("Failed to get the SpeedMult AVIF."sv);
			return false;
		}

		const auto setSneaking = REL::Relocation<uintptr_t>{ thirdPersonSpeedMultDetail::s_setSneaking }.address();
		static constexpr std::initializer_list<std::optional<uint8_t>> s_setSneakingExpected{
			0x48, 0x89, 0x5C, 0x24, std::nullopt, std::nullopt,
			0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81, 0x34, 0x01, 0x00, 0x00
		};
		if (!RELEX::Validate(setSneaking, RELEX::GetWildcardSignature(setSneaking, s_setSneakingExpected)))
		{
			Skip("Actor::SetSneaking bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		const auto moveMode = REL::Relocation<uintptr_t>{ thirdPersonSpeedMultDetail::s_doSetMoveMode }.address();
		static constexpr std::initializer_list<uint8_t> s_moveModeExpected{
			0x81, 0x61, 0x08, 0x00, 0xC0, 0xFF, 0xFF,
			0x0F, 0xB7, 0xC2, 0x25, 0xFF, 0x3F, 0x00, 0x00,
			0x09, 0x41, 0x08, 0xB0, 0x01, 0xC3
		};
		if (!RELEX::Validate(moveMode, s_moveModeExpected))
		{
			Skip("ActorState::DoSetMoveMode bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		const auto clipUpdate = REL::Relocation<uintptr_t>{ thirdPersonSpeedMultDetail::s_clipGeneratorUpdate }.address();
		static constexpr std::initializer_list<uint8_t> s_clipUpdateExpected{
			0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10, 0x56,
			0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00
		};
		if (!RELEX::Validate(clipUpdate, s_clipUpdateExpected))
		{
			Skip("hkbClipGenerator::update bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		thirdPersonSpeedMultDetail::ClipGeneratorUpdate::func = RELEX::DetourClassJump(clipUpdate, &thirdPersonSpeedMultDetail::ClipGeneratorUpdate::thunk);
		if (!thirdPersonSpeedMultDetail::ClipGeneratorUpdate::func.address())
		{
			Skip("Failed to detour hkbClipGenerator::update."sv);
			return false;
		}

		thirdPersonSpeedMultDetail::DoSetMoveMode::func = RELEX::DetourClassJump(moveMode, &thirdPersonSpeedMultDetail::DoSetMoveMode::thunk);
		if (!thirdPersonSpeedMultDetail::DoSetMoveMode::func.address())
		{
			Skip("Failed to detour ActorState::DoSetMoveMode."sv);
			return false;
		}

		thirdPersonSpeedMultDetail::SetSneaking::func = RELEX::DetourClassJump(setSneaking, &thirdPersonSpeedMultDetail::SetSneaking::thunk);
		if (!thirdPersonSpeedMultDetail::SetSneaking::func.address())
		{
			Skip("Failed to detour Actor::SetSneaking."sv);
			return false;
		}

		return true;
	}
}
