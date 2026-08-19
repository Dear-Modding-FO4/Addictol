#include <Modules/AdModuleWaterJetpackFix.h>
#include <Core/AdUtils.h>

#include <RE/A/ActiveEffect.h>
#include <RE/B/bhkCharacterController.h>
#include <RE/B/bhkPhysicsSystem.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/RTTI.h>

#define AD_NOMESSAGE_WATERJETPACK 1

namespace RE
{
	class bhkCharacterStateChangeEvent
	{
	public:
		hknpCharacterState::hknpCharacterStateType previousState;	// 00
		hknpCharacterState::hknpCharacterStateType currentState;	// 04
	};
	static_assert(sizeof(bhkCharacterStateChangeEvent) == 0x8);

	class JetpackEffect : public ActiveEffect
	{
	public:
		static constexpr auto RTTI{ RE::RTTI::JetpackEffect };
		static constexpr auto VTABLE{ RE::VTABLE::JetpackEffect };

		void JetpackStop()
		{
			using func_t = decltype(&JetpackEffect::JetpackStop);
			static REL::Relocation<func_t> func{ REL::ID{ 446875, 2226250 } };
			return func(this);
		}

		std::byte	unk098[0x3C];	// 098
		float		jetpackTime;	// 0D4
		bool		jetpackActive;	// 0D8
	};
}

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWaterJetpackFix{ "Fixes"sv, "bWaterJetpackFix"sv, true };

	namespace waterJetpackFixDetail
	{
		static void ResetJetpack(RE::Actor* a_actor) noexcept
		{
			if (!a_actor->currentProcess || !a_actor->currentProcess->middleHigh)
				return;

			for (auto& effect : a_actor->currentProcess->middleHigh->activeEffects.data)
			{
				auto* jetpack = RE::fallout_cast<RE::JetpackEffect*>(effect.get());
				if (!jetpack)
					continue;

				if (jetpack->jetpackActive)
					jetpack->JetpackStop();

				jetpack->jetpackTime = 0.0f;
				break;
			}
		}

		class CharacterStateChangeEventSink : public RE::BSTEventSink<RE::bhkCharacterStateChangeEvent>
		{
		public:
			static CharacterStateChangeEventSink* GetSingleton()
			{
				static CharacterStateChangeEventSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::bhkCharacterStateChangeEvent& a_event, RE::BSTEventSource<RE::bhkCharacterStateChangeEvent>*) override
			{
				if (a_event.currentState == RE::hknpCharacterState::hknpCharacterStateType::kUserState0) // Swimming
				{
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player)
						ResetJetpack(player);
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	ModuleWaterJetpackFix::ModuleWaterJetpackFix() :
		Module("Water Jetpack Fix", &bFixesWaterJetpackFix, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleWaterJetpackFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleWaterJetpackFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->currentProcess && player->currentProcess->middleHigh)
			{
				auto* controller = player->currentProcess->middleHigh->charController.get();
				if (controller)
				{
					static_cast<RE::BSTEventSource<RE::bhkCharacterStateChangeEvent>*>(controller)->RegisterSink
						(waterJetpackFixDetail::CharacterStateChangeEventSink::GetSingleton());
#if !AD_NOMESSAGE_WATERJETPACK
					REX::INFO("WaterJetpackFix: RegisterSink succeeded");
#endif
				}
			}
		}

		return true;
	}

}
