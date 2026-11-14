#include <Modules/AdModuleGrimReaperAPFix.h>
#include <Core/AdUtils.h>

#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/B/BGSEntryPoint.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <intrin.h>

namespace Addictol
{
	namespace grimReaperAPFixDetail
	{
		// Verified on OG / NG / AE.
		constexpr std::uintptr_t kAVOwnerInPlayer = 0x58;

		// mov ecx, 0x6B -- the entry point constant, sits right before the call.
		inline constexpr std::initializer_list<uint8_t> kEntryPointMarker{ 0xB9, 0x6B, 0x00, 0x00, 0x00 };

		// The restore call is emitted within this distance after the marker.
		constexpr std::size_t kCallSearchWindow = 32;

		using HandleEntryPointFn = bool (*)(RE::BGSEntryPoint::ENTRY_POINT, RE::Actor*, void*, void*, void*);
		using RestoreActorValueFn = void (*)(RE::ActorValueOwner*, const RE::ActorValueInfo&, float);

		HandleEntryPointFn g_originalHandleEntryPoint{ nullptr };
		RestoreActorValueFn g_originalRestoreActorValue{ nullptr };

		std::atomic<bool> g_killPending{ false };
		std::uintptr_t g_killRetAddr{ 0 };

		bool HookedHandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT a_entryPoint, RE::Actor* a_actor,
			void* a_p3, void* a_p4, void* a_p5)
		{
			if (!g_originalHandleEntryPoint)
				return false;

			if (a_entryPoint != RE::BGSEntryPoint::ENTRY_POINT::kModVATSPlayerAPKillAwardChance || !a_actor)
				return g_originalHandleEntryPoint(a_entryPoint, a_actor, a_p3, a_p4, a_p5);

			g_killRetAddr = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			g_killPending.store(true, std::memory_order_relaxed);

			return g_originalHandleEntryPoint(a_entryPoint, a_actor, a_p3, a_p4, a_p5);
		}

		void HookedRestoreActorValue(RE::ActorValueOwner* a_self, const RE::ActorValueInfo& a_info, float a_amount)
		{
			if (!g_originalRestoreActorValue)
				return;

			g_originalRestoreActorValue(a_self, a_info, a_amount);

			if (!g_killPending.load(std::memory_order_relaxed) || !g_killRetAddr)
				return;

			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_self != static_cast<RE::ActorValueOwner*>(player))
				return;

			const auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->actionPoints || &a_info != actorValues->actionPoints)
				return;

			// The restore that belongs to the kill sits in the same function, close to the entry point call.
			const auto retAddr = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			if (retAddr <= g_killRetAddr || retAddr - g_killRetAddr > 0x400)
				return;

			g_killPending.store(false, std::memory_order_relaxed);

			// Vanilla restores permanent - current, which leaves temporary modifiers unfilled.
			const auto damage = a_self->GetModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, a_info);
			if (damage < -0.5F)
				a_self->RestoreActorValue(a_info, -damage);
		}

		std::uintptr_t FindGrimReaperCallSite(std::uintptr_t a_target) noexcept
		{
			const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("Fallout4.exe"));
			if (!base)
				return 0;

			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			const auto* section = IMAGE_FIRST_SECTION(nt);

			for (uint16_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
			{
				if (std::memcmp(section[index].Name, ".text", 5) != 0)
					continue;

				const auto* begin = reinterpret_cast<const std::uint8_t*>(base + section[index].VirtualAddress);
				const auto size = static_cast<size_t>(section[index].Misc.VirtualSize);

				std::uintptr_t found = 0;
				for (size_t offset = 0; offset + kEntryPointMarker.size() <= size; ++offset)
				{
					if (std::memcmp(begin + offset, kEntryPointMarker.begin(), kEntryPointMarker.size()) != 0)
						continue;

					const auto marker = reinterpret_cast<std::uintptr_t>(begin + offset);
					for (size_t delta = kEntryPointMarker.size(); delta <= kCallSearchWindow; ++delta)
					{
						if (offset + delta + 5 > size)
							break;

						if (!RELEX::IsCallTo(marker + delta, a_target))
							continue;

						if (found)
							return 0;

						found = marker + delta;
						break;
					}
				}

				return found;
			}

			return 0;
		}
	}

	ModuleGrimReaperAPFix::ModuleGrimReaperAPFix() :
		Module("Grim Reaper's Sprint AP Fix", &bFixesGrimReaperAPFix)
	{}

	bool ModuleGrimReaperAPFix::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GrimReaperAPFix.dll"))
		{
			Skip("standalone 'GrimReaperAPFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleGrimReaperAPFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type != F4SE::MessagingInterface::kGameDataReady)
			return true;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player)
		{
			Skip("player is unavailable"sv);
			return false;
		}

		const auto ownerAddress = reinterpret_cast<std::uintptr_t>(static_cast<RE::ActorValueOwner*>(player));
		if (ownerAddress - reinterpret_cast<std::uintptr_t>(player) != grimReaperAPFixDetail::kAVOwnerInPlayer)
		{
			Skip("PlayerCharacter ActorValueOwner sub-object is not at the expected offset"sv);
			return false;
		}

		const auto entryPointTarget = REL::Relocation<std::uintptr_t>{ RE::ID::BGSEntryPoint::HandleEntryPoint }.address();
		const auto callSite = grimReaperAPFixDetail::FindGrimReaperCallSite(entryPointTarget);
		if (!callSite)
		{
			Skip("could not locate the Grim Reaper's Sprint entry point call"sv);
			return false;
		}

		const auto vtable = *reinterpret_cast<std::uintptr_t*>(ownerAddress);
		if (!vtable)
		{
			Skip("could not resolve the PlayerCharacter ActorValueOwner vtable"sv);
			return false;
		}

		grimReaperAPFixDetail::g_originalHandleEntryPoint =
			reinterpret_cast<grimReaperAPFixDetail::HandleEntryPointFn>(entryPointTarget);
		if (!RELEX::DetourCall(callSite, reinterpret_cast<uintptr_t>(&grimReaperAPFixDetail::HookedHandleEntryPoint)))
		{
			Skip("failed to detour the Grim Reaper's Sprint entry point call"sv);
			return false;
		}

		grimReaperAPFixDetail::g_originalRestoreActorValue =
			reinterpret_cast<grimReaperAPFixDetail::RestoreActorValueFn>(
				RELEX::DetourVTable(vtable, reinterpret_cast<uintptr_t>(&grimReaperAPFixDetail::HookedRestoreActorValue), 8));

		if (!grimReaperAPFixDetail::g_originalRestoreActorValue)
		{
			Skip("failed to detour ActorValueOwner::RestoreActorValue"sv);
			return false;
		}

		return true;
	}
}
