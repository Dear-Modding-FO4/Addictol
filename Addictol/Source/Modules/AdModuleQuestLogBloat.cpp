#include <Modules/AdModuleQuestLogBloat.h>
#include <Core/AdUtils.h>

#include <RE/P/PlayerCharacter.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	namespace questLogBloatDetail
	{
		using QuestLog = RE::BSSimpleList<RE::TESQuestStageItem*>;

		struct LogMatch
		{
			QuestLog::iterator prev;
			bool found{ false };
		};

		// prev is the node before the match, or the tail node when not found.
		static LogMatch FindAfter(QuestLog& a_log, QuestLog::iterator a_from, RE::TESQuestStageItem* a_item) noexcept
		{
			auto prev = a_from;
			for (auto it = std::next(a_from); it != a_log.end(); prev = it++)
			{
				if (*it == a_item)
					return { prev, true };
			}

			return { prev, false };
		}

		static bool HKAddQuestLogEntry(RE::PlayerCharacter* a_player, RE::TESQuestStageItem* a_item);
		static inline decltype(&HKAddQuestLogEntry) s_addQuestLogEntry{ nullptr };

		static bool HKAddQuestLogEntry(RE::PlayerCharacter* a_player, RE::TESQuestStageItem* a_item)
		{
			auto& log = a_player->questLog;
			const bool logged = FindAfter(log, log.before_begin(), a_item).found;
			const bool result = s_addQuestLogEntry(a_player, a_item);

			// Repeatable stage was prepended again; drop the older copy.
			if (logged && result)
			{
				if (const auto match = FindAfter(log, log.begin(), a_item); match.found)
					log.erase_after(match.prev, std::next(match.prev, 2));
			}

			return result;
		}

		static void AppendQuestLogEntry(RE::PlayerCharacter* a_player, RE::TESQuestStageItem* a_item)
		{
			auto& log = a_player->questLog;
			const auto match = FindAfter(log, log.before_begin(), a_item);
			if (!match.found)
				log.insert_after(match.prev, a_item);
		}

		struct LoadGameStub : Xbyak::CodeGenerator
		{
			LoadGameStub(uintptr_t a_resume, uintptr_t a_func)
			{
				mov(rcx, rsi);
				mov(rdx, r14);
				mov(rax, a_func);
				call(rax);

				jmp(ptr[rip]);
				dq(a_resume);
			}
		};
	}

	ModuleQuestLogBloat::ModuleQuestLogBloat() :
		Module("Quest Log Bloat", &bFixesQuestLogBloat)
	{}

	bool ModuleQuestLogBloat::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace questLogBloatDetail;

		const auto addEntryTarget = RE::ID::PlayerCharacter::AddQuestStageItem.address();
		const auto loadGameTarget = REL::Relocation{ RE::ID::PlayerCharacter::LoadGame, REL::Offset{ 0x93C, 0xD13, 0xD13 } }.address();
		const auto loadGameResume = REL::Relocation{ RE::ID::PlayerCharacter::LoadGame, REL::Offset{ 0x9B2, 0xD84, 0xD84 } }.address();

		// Validate both sites first so a mismatch never leaves a half-patch.
		// mov [rsp+8], rbx; mov [rsp+18h], rsi
		if (!RELEX::Validate(addEntryTarget, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18 }))
		{
			Skip("unexpected bytes at PlayerCharacter::AddQuestLogEntry"sv);
			return false;
		}

		// lea rbx, [rsi+7C8h]
		if (!RELEX::Validate(loadGameTarget, { 0x48, 0x8D, 0x9E, 0xC8, 0x07, 0x00, 0x00 }))
		{
			Skip("unexpected bytes at PlayerCharacter::LoadGame quest log walk"sv);
			return false;
		}

		s_addQuestLogEntry = reinterpret_cast<decltype(s_addQuestLogEntry)>(
			RELEX::DetourJump(addEntryTarget, reinterpret_cast<uintptr_t>(&HKAddQuestLogEntry)));
		if (!s_addQuestLogEntry)
		{
			Skip("failed to detour PlayerCharacter::AddQuestLogEntry"sv);
			return false;
		}

		REL::WriteSafeFill(loadGameTarget, REL::INT3, loadGameResume - loadGameTarget);
		return RELEX::XbyakJump<LoadGameStub>(loadGameTarget, loadGameResume,
			reinterpret_cast<uintptr_t>(&AppendQuestLogEntry)) != 0;
	}
}
