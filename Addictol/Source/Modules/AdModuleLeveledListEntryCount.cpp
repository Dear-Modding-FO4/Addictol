#include <Modules\ModuleLeveledListEntryCount.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bWarningsLeveledListEntryCount{"Warnings", "bLeveledListEntryCount", true};

	ModuleLeveledListEntryCount::ModuleLeveledListEntryCount() : Module("Leveled List Entry Count", &bWarningsLeveledListEntryCount)
	{
	}

	bool ModuleLeveledListEntryCount::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLeveledListEntryCount::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return false;
		}

		// we might be able to do GetFormArray<RE::TESLeveledList>() to make it a bit faster, but it needs to be tested
		auto& levItemArray = dataHandler->GetFormArray<RE::TESLevItem>();
		if (!levItemArray) {
			return false;
		}

		std::uint32_t levItemsChecked = 0;
		std::uint32_t levItemErrors = 0;

		for (RE::TESLevItem* levItem : levItemArray) {
			if (!levItem) { continue; }

			RE::TESLeveledList* levItemList = levItem->As<RE::TESLeveledList>();
			if (!levItemList) { continue; }

			levItemsChecked++;

			std::uint16_t numEntries = levItemList->baseListCount + levItemList->scriptListCount;
			if (numEntries <= 255) { continue; }

			// this leveledlist has too many entries and will cause errors

			levItemErrors++;
			REX::WARN("LeveledListEntryCount: Found LeveledList with too many entries: <FormID: {:08X} in Plugin: \"{}\">",
				levItemList->GetFormID(), levItemList->GetFilename());
		}

		REX::INFO("LeveledListEntryCount: LeveledLists checked: {}, errors found: {}", levItemsChecked, levItemErrors);

		if (levItemErrors > 0) {
			RE::ConsoleLog::GetSingleton()->AddString("Addictol::LeveledListEntryCount: Found at least one LeveledList with too many entries. This will cause crashes when the LeveledList is used ingame. Check Addictol.log for more details.");
		}

		return true;
	}

	bool ModuleLeveledListEntryCount::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleLeveledListEntryCount::DoPapyrusListener(RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}
}
