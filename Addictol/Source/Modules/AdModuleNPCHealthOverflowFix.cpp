#include <Modules/AdModuleNPCHealthOverflowFix.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>

namespace Addictol
{

	namespace npcHealthOverflowFixDetail
	{
		// Verified on OG / NG / AE.
		constexpr std::uintptr_t kAVOwnerInTESNPC = 0x130;

		// mov [rsp+8], rbx ; push rdi ; sub rsp, 0x30 ; mov rbx, rcx
		inline constexpr std::initializer_list<uint8_t> kPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x08,
			0x57,
			0x48, 0x83, 0xEC, 0x30,
			0x48, 0x8B, 0xD9 };

		// add rcx, -0x130 -- the rebase the thunk's arithmetic relies on; OG emits it before mov rdi, rdx and NG / AE after.
		inline constexpr std::initializer_list<uint8_t> kRebaseThis{ 0x48, 0x81, 0xC1, 0xD0, 0xFE, 0xFF, 0xFF };
		inline constexpr std::initializer_list<uint8_t> kMovRdiRdx{ 0x48, 0x8B, 0xFA };

		const RE::ActorValueInfo* gHealth{ nullptr };
		const RE::ActorValueInfo* gActionPoints{ nullptr };

		[[nodiscard]] bool ValidateGetActorValue(std::uintptr_t a_target) noexcept
		{
			if (!RELEX::Validate(a_target, kPrologue))
				return false;

			const auto tail = a_target + kPrologue.size();
			return RELEX::IsRuntimeOG() ?
				RELEX::Validate(tail, kRebaseThis) &&
					RELEX::Validate(tail + kRebaseThis.size(), kMovRdiRdx) :
				RELEX::Validate(tail, kMovRdiRdx) &&
					RELEX::Validate(tail + kMovRdiRdx.size(), kRebaseThis);
		}

		struct GetActorValue // TESNPC::GetActorValue()
		{
			using GetActorValue_t = float (*)(RE::ActorValueOwner* a_this, const RE::ActorValueInfo* a_info);

			static float thunk(RE::ActorValueOwner* a_this, const RE::ActorValueInfo* a_info)
			{
				const float result = original(a_this, a_info);
				if (result >= 0.0F || (a_info != gHealth && a_info != gActionPoints))
					return result;

				auto* npc = reinterpret_cast<RE::TESNPC*>(
					reinterpret_cast<std::uintptr_t>(a_this) - kAVOwnerInTESNPC);
				if (!npc->Is(RE::ENUM_FORM_ID::kNPC_) || !npc->HasAutoCalcStats())
					return result;

				// AutoCalcSkillsAttributes truncates a 32-bit seed into the int16 unclamped; the engine's own accessors read it back with movzx.
				const auto raw = a_info == gHealth ?
					npc->data.autoCalcHealth :
					npc->data.autoCalcActionPoints;
				if (result != static_cast<float>(raw))
					return result;

				return static_cast<float>(static_cast<std::uint16_t>(raw));
			}

			static inline GetActorValue_t original = nullptr;
		};

		[[nodiscard]] RE::TESNPC* FindAnyNPC()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler)
				return nullptr;

			for (auto* npc : handler->GetFormArray<RE::TESNPC>())
			{
				if (npc)
					return npc;
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

		auto* actorValues = RE::ActorValue::GetSingleton();
		if (!actorValues || !actorValues->health || !actorValues->actionPoints)
		{
			Skip("actor value list is unavailable"sv);
			return false;
		}

		// VTABLE::TESNPC[6] is REL::ID(235917), which resolves to the wrong address on AE, where the real vtable has no id.
		auto* npc = npcHealthOverflowFixDetail::FindAnyNPC();
		if (!npc)
		{
			Skip("no NPC form found to read the ActorValueOwner vtable from"sv);
			return false;
		}

		// Take the sub-object offset from the compiler instead of trusting the literal.
		const auto owner = reinterpret_cast<std::uintptr_t>(static_cast<RE::ActorValueOwner*>(npc));
		if (owner - reinterpret_cast<std::uintptr_t>(npc) != npcHealthOverflowFixDetail::kAVOwnerInTESNPC)
		{
			Skip("TESNPC ActorValueOwner sub-object is not at the expected offset"sv);
			return false;
		}

		const auto vtable = *reinterpret_cast<std::uintptr_t*>(owner);
		const auto target = *reinterpret_cast<std::uintptr_t*>(vtable + sizeof(void*)); // slot 1

		if (!npcHealthOverflowFixDetail::ValidateGetActorValue(target))
		{
			Skip("unexpected bytes at TESNPC::GetActorValue"sv);
			return false;
		}

		npcHealthOverflowFixDetail::gHealth = actorValues->health;
		npcHealthOverflowFixDetail::gActionPoints = actorValues->actionPoints;

		*((uintptr_t*)&npcHealthOverflowFixDetail::GetActorValue::original) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&npcHealthOverflowFixDetail::GetActorValue::thunk));
		return npcHealthOverflowFixDetail::GetActorValue::original != nullptr;
	}
}
