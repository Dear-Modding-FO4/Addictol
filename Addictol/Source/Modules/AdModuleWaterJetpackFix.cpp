#include <Modules/AdModuleWaterJetpackFix.h>
#include <AdUtils.h>

#include <RE/P/PlayerCharacter.h>
#include <RE/A/Actor.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesWaterJetpackFix{ "Fixes"sv, "bWaterJetpackFix"sv, true };

	namespace waterJetpackFixDetail
	{
		static bool g_wasSwimming = false;
		static std::uint32_t s_skip = 0;

		static RE::PlayerCharacter* GetPlayerSafe() noexcept
		{
			auto handle = RE::PlayerCharacter::GetPlayerHandle();
			if (!handle) return nullptr;
			auto ptr = handle.get();
			return ptr ? static_cast<RE::PlayerCharacter*>(ptr.get()) : nullptr;
		}

		static void ResetJumpState(RE::PlayerCharacter* a_player) noexcept
		{
			if (!a_player->currentProcess || !a_player->currentProcess->middleHigh)
				return;
			auto* ctrl = a_player->currentProcess->middleHigh->charController.get();
			if (!ctrl) return;

			ctrl->flags &= ~0x2000;
			ctrl->flags |= 0x400;
			ctrl->fallTime = 0.0f;
			ctrl->fallStartHeight = 0.0f;
			ctrl->inAirPreMove = false;
			ctrl->context.m_currentState = RE::hknpCharacterState::hknpCharacterStateType::kOnGround;
		}

		static void OnFrameUpdate() noexcept
		{
			if (++s_skip < 10) return;
			s_skip = 0;

			auto* player = GetPlayerSafe();
			if (!player) return;

			bool isSwimming = player->IsSwimming();
			if (isSwimming == g_wasSwimming) return;

			if (isSwimming)
				ResetJumpState(player);

			g_wasSwimming = isSwimming;
		}

		struct PlayerUpdateHook
		{
			static void thunk(RE::Actor* a_this, float a_delta)
			{
				func(a_this, a_delta);
				if (a_this && a_this->IsPlayerRef())
					OnFrameUpdate();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleWaterJetpackFix::ModuleWaterJetpackFix() :
		Module("Water Jetpack Fix", &bFixesWaterJetpackFix)
	{}

	bool ModuleWaterJetpackFix::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleWaterJetpackFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Install only at GameDataReady stage (player + process data valid)
		if (a_msg)
		{
			if (a_msg->type != F4SE::MessagingInterface::kGameDataReady)
				return true;
		}

		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCharacter[0] };
		waterJetpackFixDetail::PlayerUpdateHook::func = vtbl.write_vfunc(0xCF, waterJetpackFixDetail::PlayerUpdateHook::thunk);
		REX::INFO("WaterJetpackFix: PlayerCharacter Update hook installed (slot 0xCF)"sv);

		return true;
	}

	bool ModuleWaterJetpackFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleWaterJetpackFix::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
