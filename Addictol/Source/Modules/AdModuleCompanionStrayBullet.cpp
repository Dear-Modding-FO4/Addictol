#include <Modules/AdModuleCompanionStrayBullet.h>
#include <AdUtils.h>

#include <RE/A/Actor.h>
#include <RE/E/ENUM_FORM_ID.h>
#include <RE/G/GUN_STATE.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/S/SIT_SLEEP_STATE.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

namespace RE
{
	class PickRefUpdateEvent
	{
	public:
		ObjectRefHandle pickRef; // 00
	};
	static_assert(sizeof(PickRefUpdateEvent) == 0x4);
}

namespace Addictol
{
	static REX::TOML::Bool<> bFixesCompanionStrayBullet{"Fixes"sv, "bCompanionStrayBullet"sv, true};

	namespace companionStrayBulletDetail
	{
		// Debounce window: at most one re-equip pass per 300 ms of combat-exit churn.
		static inline std::atomic<std::int64_t> s_lastPatchMs{0};

		static std::int64_t NowMs() noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now().time_since_epoch())
				.count();
		}

		static std::vector<RE::Actor *> GetPlayerTeammates()
		{
			std::vector<RE::Actor *> teammates;

			const auto processLists = RE::ProcessLists::GetSingleton();
			if (processLists)
			{
				for (auto &handle : processLists->highActorHandles)
				{
					auto *actor = handle.get().get();
					// niFlags bit 26 (0x1a) marks an actor as a player teammate.
					if (actor && (actor->niFlags.flags >> 0x1a & 1) != 0)
						teammates.push_back(actor);
				}
			}

			if (!teammates.empty())
			{
				std::sort(teammates.begin(), teammates.end());
				teammates.erase(std::unique(teammates.begin(), teammates.end()), teammates.end());
			}

			return teammates;
		}

		struct HandleOnCombatExit
		{
			static std::uint8_t thunk(std::uintptr_t a1, std::uintptr_t a2, std::uint32_t a3, std::uint32_t a4, std::uint8_t a5, std::uint32_t a6)
			{
				// Fast path: runs on every combat-state change, so no logging/allocation here.
				if (a4 != 0x46)
					return func(a1, a2, a3, a4, a5, a6);

				const std::int64_t now = NowMs();
				std::int64_t last = s_lastPatchMs.load(std::memory_order_relaxed);
				if (now - last > 300 &&
					s_lastPatchMs.compare_exchange_strong(last, now, std::memory_order_relaxed))
				{
					auto teammates = GetPlayerTeammates();
					if (!teammates.empty())
					{
						for (auto *actor : teammates)
						{
							if (actor)
								actor->HandleItemEquip(true);
						}

						REX::INFO("CompanionStrayBullet: re-equipped weapons on {} player teammate(s) on combat exit."sv, teammates.size());
					}
				}

				return func(a1, a2, a3, a4, a5, a6);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Records the crosshair pick ref announced by the game so Hook A doesn't have to read a per-runtime data-global
		class CrosshairPickSink : public RE::BSTEventSink<RE::PickRefUpdateEvent>
		{
		public:
			static inline std::atomic<std::uint32_t> s_crosshairHandle{0};

			static CrosshairPickSink *GetSingleton()
			{
				static CrosshairPickSink instance;
				return &instance;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::PickRefUpdateEvent &a_event, RE::BSTEventSource<RE::PickRefUpdateEvent> *) override
			{
				// ObjectRefHandle has no const raw-value accessor; it is a single uint32 at offset 0.
				std::uint32_t raw = 0;
				std::memcpy(&raw, &a_event.pickRef, sizeof(raw));
				s_crosshairHandle.store(raw, std::memory_order_relaxed);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		static RE::Actor *GetCurrentCrosshairActor()
		{
			const std::uint32_t raw = CrosshairPickSink::s_crosshairHandle.load(std::memory_order_relaxed);
			if (raw == 0)
				return nullptr;

			// Rebuild the handle from its raw value (ObjectRefHandle is a single uint32).
			RE::ObjectRefHandle handle{};
			std::memcpy(&handle, &raw, sizeof(raw));

			auto refPtr = handle.get();
			RE::TESObjectREFR *ref = refPtr.get();
			if (ref && ref->GetSavedFormType() == RE::ENUM_FORM_ID::kACHR)
				return static_cast<RE::Actor *>(ref);
			
			return nullptr;
		}

		struct HandleOnCommandEnter
		{
			static void thunk(RE::Actor *akPlayer, char abCommandState, std::uint8_t unk)
			{
				RE::Actor *crosshairActor = GetCurrentCrosshairActor();

				bool exit = false;
				if (!akPlayer)
					exit = true;

				if (!crosshairActor)
					exit = true;

				if (crosshairActor && crosshairActor->weaponState == RE::WEAPON_STATE::kSheathed)
					exit = true;

				if (crosshairActor && crosshairActor->gunState == RE::GUN_STATE::kRelaxed)
					exit = true;

				if (crosshairActor && crosshairActor->DoGetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal)
					exit = true;

				if (akPlayer && !akPlayer->IsInCombat())
					exit = true;

				if (!exit)
					crosshairActor->HandleItemEquip(true);

				func(akPlayer, abCommandState, unk);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Registers the crosshair sink exactly once
		static void RegisterCrosshairPickSink()
		{
			static std::atomic<bool> s_registered{false};
			if (s_registered.load(std::memory_order_acquire))
				return;

			auto *player = RE::PlayerCharacter::GetSingleton();
			if (!player)
			{
				REX::WARN("CompanionStrayBullet: PlayerCharacter unavailable; crosshair sink not registered (Hook A idle)."sv);
				return;
			}

			bool expected = false;
			if (!s_registered.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				return;

			static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent> *>(player)->RegisterSink(CrosshairPickSink::GetSingleton());
			REX::INFO("CompanionStrayBullet: registered crosshair pick-ref sink (Hook A)."sv);
		}
	}

	ModuleCompanionStrayBullet::ModuleCompanionStrayBullet() :
		Module("Companion Stray Bullet", &bFixesCompanionStrayBullet)
	{}

	bool ModuleCompanionStrayBullet::DoQuery() const noexcept
	{
		if (REX::W32::GetModuleHandleW(L"FollowerStrayBulletFix.dll")) 
		{
			REX::WARN("CompanionStrayBullet: Fix disabled, Companion Shoots At Player Fix is installed."sv);
			return false;
		}

		return true;
	}

	bool ModuleCompanionStrayBullet::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		// kGameDataReady stage: PlayerCharacter + its event source are valid, register the sink.
		if (a_msg)
		{
			if (a_msg->type == F4SE::MessagingInterface::kGameDataReady)
				companionStrayBulletDetail::RegisterCrosshairPickSink();
			
			return true;
		}

		// Load block: install both call hooks, each behind its own call-target guard
		auto &trampoline = REL::GetTrampoline();
		bool installed = false;

		// Combat-exit dialogue CALL site inside CombatController::SetTarget (NG == AE, OG differs).
		{
			REL::Relocation<std::uintptr_t> site{ REL::ID{ 369646, 2216401 }, REL::Offset{ 0x1B8, 0x257 } };

			// 1. must be an E8 rel32 CALL
			if (*reinterpret_cast<const std::uint8_t *>(site.address()) != 0xE8)
				REX::WARN("CompanionStrayBullet: combat-exit call-site is not a CALL (E8); Hook B not applied."sv);
			else
			{
				// 2. must still point at vanilla HandleOnCombatExit (else another mod redirected it, or wrong build)
				const std::int32_t rel = *reinterpret_cast<const std::int32_t *>(site.address() + 1);
				const std::uintptr_t callTarget = site.address() + 5 + rel;
				REL::Relocation<std::uintptr_t> expectedCallee{ REL::ID{ 1512408, 2238049 } };

				if (callTarget != expectedCallee.address())
					REX::WARN("CompanionStrayBullet: combat-exit call target unexpected (already patched or unsupported build). Hook B not applied."sv);
				else
				{
					// 3. safe to patch
					companionStrayBulletDetail::s_lastPatchMs.store(companionStrayBulletDetail::NowMs(), std::memory_order_relaxed);
					companionStrayBulletDetail::HandleOnCombatExit::func =
						trampoline.write_call<5>(site.address(), companionStrayBulletDetail::HandleOnCombatExit::thunk);
					installed = true;
				}
			}
		}

		{
			REL::Relocation<std::uintptr_t> site{ REL::ID{ 1512511, 2229916 }, REL::Offset{ 0x118, 0x11E } };

			// 1. must be an E8 rel32 CALL
			if (*reinterpret_cast<const std::uint8_t *>(site.address()) != 0xE8)
				REX::WARN("CompanionStrayBullet: command-enter call-site is not a CALL (E8); Hook A not applied."sv);
			else
			{
				// 2. must still point at the vanilla command-enter handler
				const std::int32_t rel = *reinterpret_cast<const std::int32_t *>(site.address() + 1);
				const std::uintptr_t callTarget = site.address() + 5 + rel;
				REL::Relocation<std::uintptr_t> expectedCallee{ REL::ID{ 354559, 2233152 } };

				if (callTarget != expectedCallee.address())
					REX::WARN("CompanionStrayBullet: command-enter call target unexpected (already patched or unsupported build). Hook A not applied."sv);
				else
				{
					// 3. safe to patch
					companionStrayBulletDetail::HandleOnCommandEnter::func =
						trampoline.write_call<5>(site.address(), companionStrayBulletDetail::HandleOnCommandEnter::thunk);
					installed = true;
				}
			}
		}

		// Fatal only if neither hook could be applied
		return installed;
	}

	bool ModuleCompanionStrayBullet::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleCompanionStrayBullet::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}
}
