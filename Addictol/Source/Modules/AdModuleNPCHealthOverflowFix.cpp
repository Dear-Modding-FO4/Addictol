#include <Modules/AdModuleNPCHealthOverflowFix.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorValueInfo.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>

namespace Addictol
{

	namespace npcHealthOverflowFixDetail
	{
		// TESActorBase::ActorValueOwner sub-object, same offset on OG / NG / AE.
		constexpr std::uintptr_t kAVOwnerInTESNPC = 0x130;

		// Hand-written negative health offsets stay small, the int16 wraparound lands
		// around -31k, so anything below -1000 is a wrap.
		constexpr int16_t kNegativeThreshold = -1000;

		// TESNPC::GetActorValue prologue, byte-identical on every runtime:
		//   mov [rsp+8], rbx ; push rdi ; sub rsp, 0x30 ; mov rbx, rcx
		inline constexpr std::initializer_list<uint8_t> kPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x08,
			0x57,
			0x48, 0x83, 0xEC, 0x30,
			0x48, 0x8B, 0xD9 };

		// OG then walks the sub-object pointer back to the NPC, NG / AE just keep going.
		inline constexpr std::initializer_list<uint8_t> kPrologueTailOG{ 0x48, 0x81, 0xC1, 0xD0, 0xFE, 0xFF, 0xFF };
		inline constexpr std::initializer_list<uint8_t> kPrologueTailNGAE{ 0x48, 0x8B, 0xFA };

		struct GetActorValue // TESNPC::GetActorValue()
		{
			using GetActorValue_t = float (*)(RE::ActorValueOwner* a_this, const RE::ActorValueInfo* a_info);

			static float thunk(RE::ActorValueOwner* a_this, const RE::ActorValueInfo* a_info)
			{
				const float result = original(a_this, a_info);
				if (result >= 0.0F)
					return result;

				auto* npc = reinterpret_cast<RE::TESNPC*>(
					reinterpret_cast<std::uintptr_t>(a_this) - kAVOwnerInTESNPC);
				if (!npc || !npc->HasAutoCalcStats())
					return result;

				// With auto-calc stats on, the level-derived health / AP seed is stored into an
				// int16 with no clamping. Past level ~120 the value passes 32767 and wraps, and
				// the engine sign-extends it, so the NPC's base HP reads as roughly -31k and
				// it dies to the first hit. The wrap is modular, adding 65536 recovers the
				// intended value. Matching the returned float against the raw int16 is what
				// tells us the value really came from the sign extension and not from
				// race / class / perk additions.
				const auto health = npc->data.autoCalcHealth;
				if (health < kNegativeThreshold && result == static_cast<float>(health))
					return result + 65536.0F;

				const auto actionPoints = npc->data.autoCalcActionPoints;
				if (actionPoints < kNegativeThreshold && result == static_cast<float>(actionPoints))
					return result + 65536.0F;

				return result;
			}

			static inline GetActorValue_t original = nullptr;
		};

		[[nodiscard]] RE::TESNPC* FindAnyNPC()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler)
				return nullptr;

			for (auto* form : handler->formArrays[std::to_underlying(RE::ENUM_FORM_ID::kNPC_)])
			{
				if (form)
					return form->As<RE::TESNPC>();
			}

			return nullptr;
		}
	}

	ModuleNPCHealthOverflowFix::ModuleNPCHealthOverflowFix() :
		Module("NPC Health Overflow Fix", &bFixesNPCHealthOverflowFix)
	{}

	bool ModuleNPCHealthOverflowFix::DoQuery() const noexcept
	{
		if (IsModDLLPresent("AutoCalcOverflowFix.dll"))
		{
			Skip("standalone 'AutoCalcOverflowFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleNPCHealthOverflowFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type != F4SE::MessagingInterface::kGameDataReady)
			return true;

		// TESNPC overrides GetActorValue on its ActorValueOwner sub-object, but there is no
		// address library id for that vtable, so borrow it from a live NPC. Every instance
		// shares it, one is enough.
		auto* npc = npcHealthOverflowFixDetail::FindAnyNPC();
		if (!npc)
		{
			Skip("no NPC form found to read the ActorValueOwner vtable from"sv);
			return false;
		}

		const auto owner = reinterpret_cast<std::uintptr_t>(npc) + npcHealthOverflowFixDetail::kAVOwnerInTESNPC;
		const auto vtable = *reinterpret_cast<std::uintptr_t*>(owner);
		const auto target = *reinterpret_cast<std::uintptr_t*>(vtable + sizeof(void*)); // slot 1

		const auto tail = RELEX::IsRuntimeOG() ? npcHealthOverflowFixDetail::kPrologueTailOG : npcHealthOverflowFixDetail::kPrologueTailNGAE;
		if (!RELEX::Validate(target, npcHealthOverflowFixDetail::kPrologue) ||
			!RELEX::Validate(target + npcHealthOverflowFixDetail::kPrologue.size(), tail))
		{
			REX::WARN("NPC Health Overflow Fix: unexpected bytes at TESNPC::GetActorValue, skipping to avoid corruption."sv);
			return false;
		}

		*((uintptr_t*)&npcHealthOverflowFixDetail::GetActorValue::original) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&npcHealthOverflowFixDetail::GetActorValue::thunk));
		return npcHealthOverflowFixDetail::GetActorValue::original != nullptr;
	}
}
