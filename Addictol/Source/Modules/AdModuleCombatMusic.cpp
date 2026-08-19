#include <Modules/AdModuleCombatMusic.h>
#include <Core/AdUtils.h>
#include <Core/AdGameUtils.h>

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDeathEvent.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesCombatMusic{ "Fixes"sv, "bCombatMusic"sv, true };

	namespace combatMusicDetail
	{
		static inline constexpr std::array<std::string_view, 9> commands =
		{
			"RemoveMusic MUSzCombat"sv,
			"RemoveMusic MUSzCombatBoss"sv,
			"RemoveMusic MUSzCombatBossLegendary"sv,
			"RemoveMusic MUSzCombatHigh"sv,
			"RemoveMusic MUSzCombatInst307"sv,
			"RemoveMusic MUSzCombatInst307Boss"sv,
			"RemoveMusic MUSzCombatMassFusion"sv,
			"RemoveMusic MUSzDLC01CombatMechanist"sv,
			"RemoveMusic MUSzDLC01CombatMechanistMinions"sv,
		};

		static std::atomic<bool> fixQueued { false };

		void FixCombatMusic() noexcept
		{
			if (fixQueued.exchange(true))
				return;
			
				std::jthread([]() {
					std::this_thread::sleep_for(std::chrono::seconds(5));

					F4SE::GetTaskInterface()->AddTask([]() {
						for (const std::string_view& command : commands)
							ExecuteCommand(command, nullptr, true);

						fixQueued = false;
					});
				}).detach();
		}

		bool NeedToFixCombatMusic() noexcept
		{
			auto* player = RE::PlayerCharacter::GetSingleton();

			if (!player)
				return false;

			return !player->IsInCombat();
		}

		class DeathEventHandler : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			[[nodiscard]] static DeathEventHandler* GetSingleton()
			{
				static DeathEventHandler singleton;
				return std::addressof(singleton);
			}
			
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent& a_event, RE::BSTEventSource<RE::TESDeathEvent>*)
			{
				if (a_event.dead == true)
					return RE::BSEventNotifyControl::kContinue;

				if (a_event.actorDying.get() == nullptr || a_event.actorKiller.get() == nullptr)
					return RE::BSEventNotifyControl::kContinue;

				if (a_event.actorKiller.get() != RE::PlayerCharacter::GetSingleton())
					return RE::BSEventNotifyControl::kContinue;

				if (NeedToFixCombatMusic())
					FixCombatMusic();

				return RE::BSEventNotifyControl::kContinue;
			}

			DeathEventHandler() = default;
			DeathEventHandler(const DeathEventHandler&) = delete;
			DeathEventHandler(DeathEventHandler&&) = delete;
			~DeathEventHandler() = default;
			DeathEventHandler& operator=(const DeathEventHandler&) = delete;
			DeathEventHandler& operator=(DeathEventHandler&&) = delete;
		};
	}

	ModuleCombatMusic::ModuleCombatMusic() :
		Module("Combat Music", &bFixesCombatMusic, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleCombatMusic::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			RE::TESDeathEvent::GetEventSource()->RegisterSink(combatMusicDetail::DeathEventHandler::GetSingleton());

		return true;
	}

	bool ModuleCombatMusic::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
		{
			if (combatMusicDetail::NeedToFixCombatMusic())
				combatMusicDetail::FixCombatMusic();
		}

		return true;
	}

}
