#include <Modules/AdModuleSprintSpeedMult.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorState.h>
#include <RE/A/ActorValue.h>
#include <RE/B/BSAnimationGraphManager.h>
#include <RE/H/hkStringPtr.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <cmath>

namespace Addictol
{
	namespace sprintSpeedMultDetail
	{
		constexpr REL::ID s_doSetMoveMode{ 196994, 2231463 };
		constexpr REL::ID s_clipGeneratorUpdate{ 935260, 2261061 };

		// BShkbAnimationGraph embeds its hkbCharacter and stores the driven 3D root.
		constexpr size_t s_graphCharacter = 0x1C8;
		constexpr size_t s_graphTarget = 0x388;
		constexpr size_t s_clipName = 0x38;
		constexpr size_t s_clipPlaybackSpeed = 0xB0;

		// Third-person clips whose root motion carries the player while sprinting.
		constexpr std::string_view s_sprintClips[]{
			"WPNSprint"sv, "WPNSprint_Jump"sv,
			"SprintForward"sv, "SprintLeanLeft"sv, "SprintLeanRight"sv,
			"SprintForward00"sv, "SprintLeanLeft00"sv, "SprintLeanRight00"sv
		};

		RE::ActorValueInfo* s_speedMultAVIF;
		std::atomic<uintptr_t> s_playerCharacter;
		std::atomic<float> s_sprintFactor{ 1.0f };

		[[nodiscard]] static float GetSprintFactor(RE::PlayerCharacter* a_player)
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

		[[nodiscard]] static bool IsSprintClip(uintptr_t a_clip)
		{
			const auto* name = reinterpret_cast<const RE::hkStringPtr*>(a_clip + s_clipName)->c_str();
			return name && std::ranges::find(s_sprintClips, std::string_view{ name }) != std::end(s_sprintClips);
		}

		struct DoSetMoveMode
		{
			static bool thunk(RE::ActorState* a_state, uint16_t a_flags)
			{
				const auto result = func(a_state, a_flags);
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (player && a_state == static_cast<RE::ActorState*>(player))
				{
					// Cache only while sprinting; a rebuilt graph may reuse the old address.
					if (!(a_flags & 0x100))
					{
						s_playerCharacter.store(0, std::memory_order_relaxed);
					}
					else
					{
						const auto factor = GetSprintFactor(player);
						s_sprintFactor.store(factor, std::memory_order_relaxed);
						s_playerCharacter.store(FindThirdPersonCharacter(player), std::memory_order_relaxed);

						// Behavior mods may bind their own sprint clips to this variable.
						static const RE::BSFixedString s_sprintPlaybackSpeed{ "fLocomotionSprintPlaybackSpeed"sv };
						player->SetGraphVariableFloat(s_sprintPlaybackSpeed, factor);
					}
				}

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
				if (character && character == s_playerCharacter.load(std::memory_order_relaxed) && IsSprintClip(a_clip))
				{
					// Same effect as a graph binding: the member feeds update and sync this frame.
					*reinterpret_cast<float*>(a_clip + s_clipPlaybackSpeed) = s_sprintFactor.load(std::memory_order_relaxed);
				}

				func(a_clip, a_context, a_timestep);
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

		const auto moveMode = REL::Relocation<uintptr_t>{ sprintSpeedMultDetail::s_doSetMoveMode }.address();
		// Keep upper bits at +0x08, set the low 14 move-mode bits, return true.
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

		const auto clipUpdate = REL::Relocation<uintptr_t>{ sprintSpeedMultDetail::s_clipGeneratorUpdate }.address();
		// mov r11, rsp; mov [r11+10h], rbx; push rsi; sub rsp, 0B0h
		static constexpr std::initializer_list<uint8_t> s_clipUpdateExpected{
			0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10, 0x56,
			0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00
		};
		if (!RELEX::Validate(clipUpdate, s_clipUpdateExpected))
		{
			Skip("hkbClipGenerator::update bytes do not match; unsupported runtime or another plugin owns the entry"sv);
			return false;
		}

		sprintSpeedMultDetail::ClipGeneratorUpdate::func = RELEX::DetourClassJump(clipUpdate, &sprintSpeedMultDetail::ClipGeneratorUpdate::thunk);
		if (!sprintSpeedMultDetail::ClipGeneratorUpdate::func.address())
		{
			Skip("Failed to detour hkbClipGenerator::update."sv);
			return false;
		}

		sprintSpeedMultDetail::DoSetMoveMode::func = RELEX::DetourClassJump(moveMode, &sprintSpeedMultDetail::DoSetMoveMode::thunk);
		if (!sprintSpeedMultDetail::DoSetMoveMode::func.address())
		{
			Skip("Failed to detour ActorState::DoSetMoveMode."sv);
			return false;
		}

		return true;
	}
}
