#include <Modules\AdModuleDuplicateAddonNodeIndex.h>
#include <AdUtils.h>

#include <RE/B/BGSAddonNode.h>
#include <RE/C/ConsoleLog.h>
#include <RE/T/TESDataHandler.h>

namespace Addictol
{
	static REX::TOML::Bool<> bWarningsDuplicateAddonNodeIndex{"Warnings", "bDuplicateAddonNodeIndex", true};

	ModuleDuplicateAddonNodeIndex::ModuleDuplicateAddonNodeIndex() : Module("Duplicate Addon Node Index", &bWarningsDuplicateAddonNodeIndex)
	{
	}

	bool ModuleDuplicateAddonNodeIndex::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		RE::TESDataHandler *dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
		{
			return false;
		}

		std::unordered_map<uint32_t, RE::BGSAddonNode *> addonNodeMap;

		auto &addonNodeArray = dataHandler->GetFormArray<RE::BGSAddonNode>();
		if (addonNodeArray.empty())
		{
			return false;
		}

		std::uint32_t addonNodesChecked = 0;
		std::uint32_t addonNodeErrors = 0;

		for (RE::BGSAddonNode *addonNode : addonNodeArray)
		{
			if (!addonNode)
			{
				continue;
			}

			const auto result = addonNodeMap.insert(std::make_pair(addonNode->index, addonNode));
			if (!result.second)
			{
				// addonnode index already found in map
				const auto currentAddonNode = (*result.first).second;

				// Files
				auto* addonNodeFile = addonNode->GetFile(0);
				auto* currentAddonNodeFile = currentAddonNode->GetFile(0);

				REX::WARN("DuplicateAddonNodeIndex: Found duplicate AddonNode index ({}) between the following AddonNodes: <FormID: {:08X} in Plugin: \"{}\"> and <FormID: {:08X} in Plugin: \"{}\">",
						  addonNode->index,
						  currentAddonNode->GetFormID(), currentAddonNodeFile ? currentAddonNodeFile->GetFilename() : "MODNAME_NOT_FOUND",
						  addonNode->GetFormID(), addonNodeFile ? addonNodeFile->GetFilename() : "MODNAME_NOT_FOUND");

				addonNodeErrors++;
			}

			addonNodesChecked++;
		}

		REX::INFO("DuplicateAddonNodeIndex: AddonNodes checked: {}, errors found: {}", addonNodesChecked, addonNodeErrors);

		if (addonNodeErrors > 0)
		{
			// we can remove this if it isn't necessary -bp42s
			RE::ConsoleLog::GetSingleton()->AddString("Addictol::DuplicateAddonNodeIndex: Duplicate AddonNode indexes were detected. This will cause issues with visual effects. Check Addictol.log for more details.");
		}

		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoPapyrusListener(RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}
}
