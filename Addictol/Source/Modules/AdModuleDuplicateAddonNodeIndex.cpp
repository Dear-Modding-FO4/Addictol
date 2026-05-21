#include <Modules/AdModuleDuplicateAddonNodeIndex.h>
#include <AdUtils.h>

#include <RE/B/BGSAddonNode.h>
#include <RE/C/ConsoleLog.h>
#include <RE/T/TESDataHandler.h>

namespace Addictol
{
	static REX::TOML::Bool<> bWarningsDuplicateAddonNodeIndex{"Warnings"sv, "bDuplicateAddonNodeIndex"sv, true};

	ModuleDuplicateAddonNodeIndex::ModuleDuplicateAddonNodeIndex()
		: Module("Duplicate Addon Node Index", &bWarningsDuplicateAddonNodeIndex)
	{}

	bool ModuleDuplicateAddonNodeIndex::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		if (!a_msg)
			return true;
		
		RE::TESDataHandler *dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return false;

		const auto& addonNodeArray = dataHandler->GetFormArray<RE::BGSAddonNode>();
		if (addonNodeArray.empty())
			return false;

		std::unordered_map<uint32_t, std::vector<RE::BGSAddonNode *>> addonNodeMap;
		addonNodeMap.reserve(addonNodeArray.size());
		uint32_t addonNodesChecked = 0;
		std::size_t addonNodeErrors = 0;

		// build map based on indexes
		for (RE::BGSAddonNode *addonNode : addonNodeArray)
		{
			if (!addonNode)
				continue;

			addonNodeMap[addonNode->index].push_back(addonNode);
			addonNodesChecked++;
		}

		// check collisions
		for (auto &[index, nodes] : addonNodeMap)
		{
			std::size_t nodesSize = nodes.size();
			if (nodesSize <= 1)
				continue;

			addonNodeErrors += 1;

			std::string nodesErrorMessage = "";
			for (RE::BGSAddonNode *node : nodes)
			{
				if (!node)
					continue;

				nodesErrorMessage += GetFormInfo(node);
			}
			nodesErrorMessage = std::format("{{}}"sv, nodesErrorMessage);

			REX::WARN("DuplicateAddonNodeIndex: Index ({}) is shared by {} the following AddonNodes: {}"sv,
					  index, nodesSize, nodesErrorMessage);
		}

		REX::INFO("DuplicateAddonNodeIndex: AddonNodes checked: {}, errors found: {}"sv, addonNodesChecked, addonNodeErrors);

		if (addonNodeErrors > 0)
		{
			RE::ConsoleLog::GetSingleton()->AddString("Addictol::DuplicateAddonNodeIndex: Duplicate AddonNode indexes were detected."
													  " This will cause issues with visual effects. Check Addictol.log for more details.");
		}

		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleDuplicateAddonNodeIndex::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}
}
