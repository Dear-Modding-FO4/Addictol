#include <Modules/AdModuleNoEssentialDecapitation.h>
#include <AdUtils.h>

#include <RE/N/NiAVObject.h>
#include <RE/N/NiNode.h>
#include <RE/T/TESFormUtil.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/A/Actor.h>
#include <RE/T/TESNPC.h>
#include <RE/A/ACTOR_LIFE_STATE.h>
#include <RE/B/bhkWorld.h>

#include <cstdint>
#include <cstring>
#include <atomic>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesNoEssentialDecapitation{ "Fixes"sv, "bNoEssentialDecapitation"sv, true };

	namespace noEssentialDecapitationDetail
	{
		using SetDismemberedLimbFn = bool(RE::NiAVObject*, bool, bool);

		static void* g_trampoline = nullptr;

		static bool Hook_SetDismemberedLimb(RE::NiAVObject* a_object, bool a_tf, bool a_recurse)
		{
			auto* tramp = g_trampoline;
			if (!tramp)
				return false;

			if (!a_object || !a_tf)
				return reinterpret_cast<SetDismemberedLimbFn*>(tramp)(a_object, a_tf, a_recurse);

			RE::TESObjectREFR* refr = nullptr;
			{
				int guard = 0;
				for (RE::NiAVObject* node = a_object; node && guard < 16; node = reinterpret_cast<RE::NiAVObject*>(node->parent), ++guard)
				{
					const auto ud = node->userData;
					if (!ud) continue;
					auto* candidate = reinterpret_cast<RE::TESObjectREFR*>(ud);
					if (!candidate) continue;
					const auto ft = candidate->GetFormType();
					if (ft == RE::ENUM_FORM_ID::kACHR || ft == RE::ENUM_FORM_ID::kREFR)
					{
						refr = candidate;
						break;
					}
				}
			}

			if (refr)
			{
				auto* actor = refr->As<RE::Actor>();
				if (actor)
				{
					bool isEss = actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential);
					bool isProt = actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kProtected);

					if (!isEss && !isProt)
					{
						auto* npc = actor->GetNPC();
						if (npc)
						{
							if (!isEss) isEss = npc->IsEssential();
							if (!isProt) isProt = npc->IsProtected();
						}
					}

					if (isEss || isProt)
					{
						const auto ls = static_cast<std::uint32_t>(actor->lifeState);
						const char* name = "(unnamed)";
						auto* npc2 = actor->GetNPC();
						if (npc2 && npc2->GetFullName())
							name = npc2->GetFullName();

						if (isEss)
						{
							REX::INFO("NoEssentialDecapitation: BLOCKED Essential \"{}\" (0x{:08X}) lifeState={}"sv,
								name, actor->GetFormID(), ls);
							return false;
						}

						if (ls == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kDead) ||
							ls == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kDying))
						{
							REX::INFO("NoEssentialDecapitation: ALLOWED Protected \"{}\" (0x{:08X}) already dead/dying (lifeState={})"sv,
								name, actor->GetFormID(), ls);
						}
						else
						{
							REX::INFO("NoEssentialDecapitation: BLOCKED Protected \"{}\" (0x{:08X}) still alive (lifeState={})"sv,
								name, actor->GetFormID(), ls);
							return false;
						}
					}
				}
			}

			return reinterpret_cast<SetDismemberedLimbFn*>(tramp)(a_object, a_tf, a_recurse);
		}

		// 14-byte absolute JMP helper (FF 25 00 00 00 00 <addr64>)
#pragma pack(push, 1)
		struct JMP14
		{
			std::uint8_t  op1 = 0xFF;
			std::uint8_t  op2 = 0x25;
			std::uint32_t rel = 0;
			std::uint64_t addr;
		};
#pragma pack(pop)
		static_assert(sizeof(JMP14) == 14);

		static void InstallHook() noexcept
		{
			const auto addr = RE::ID::bhkWorld::SetDismemberedLimb.address();
			if (!addr)
			{
				REX::ERROR("NoEssentialDecapitation: SetDismemberedLimb address resolve failed"sv);
				return;
			}
			REX::INFO("NoEssentialDecapitation: SetDismemberedLimb = 0x{:016X}"sv, addr);

			// 14-byte absolute JMP is position-insensitive, so we can safely relocate
			// the first 14 bytes of the original function into a trampoline and jump
			// back to addr+14. This stays compatible with other chain hooks.
			constexpr std::size_t kJmp14Size = 14;
			std::byte saved[16]{};
			auto* target = reinterpret_cast<const std::uint8_t*>(addr);
			std::memcpy(saved, target, kJmp14Size);

			auto& tramp = REL::GetTrampoline();
			g_trampoline = tramp.allocate(kJmp14Size + sizeof(JMP14));
			if (!g_trampoline)
			{
				REX::ERROR("NoEssentialDecapitation: trampoline alloc failed"sv);
				return;
			}

			std::memcpy(g_trampoline, saved, kJmp14Size);
			JMP14 jmpBack{};
			jmpBack.addr = static_cast<std::uint64_t>(addr + kJmp14Size);
			std::memcpy(static_cast<std::byte*>(g_trampoline) + kJmp14Size, &jmpBack, sizeof(jmpBack));

			JMP14 jmp14{};
			jmp14.addr = reinterpret_cast<std::uint64_t>(&Hook_SetDismemberedLimb);
			REL::WriteSafe(addr, reinterpret_cast<std::byte*>(&jmp14), kJmp14Size);

			std::atomic_thread_fence(std::memory_order_seq_cst);
			REX::INFO("NoEssentialDecapitation: SetDismemberedLimb hook installed"sv);
		}
	}

	ModuleNoEssentialDecapitation::ModuleNoEssentialDecapitation() :
		Module("No Essential Decapitation", &bFixesNoEssentialDecapitation)
	{}

	bool ModuleNoEssentialDecapitation::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleNoEssentialDecapitation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Install at GameDataReady stage
		if (a_msg)
		{
			if (a_msg->type != F4SE::MessagingInterface::kGameDataReady)
				return true;
		}

		noEssentialDecapitationDetail::InstallHook();
		return true;
	}

	bool ModuleNoEssentialDecapitation::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleNoEssentialDecapitation::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
