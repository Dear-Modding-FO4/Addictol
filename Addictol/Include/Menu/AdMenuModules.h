#pragma once

#include <Core/AdModuleOutcome.h>

#include <array>
#include <string_view>

namespace Addictol::Menu
{
	enum class ModuleOutcomeSeverity : uint8_t
	{
		kNormal,
		kDisabled,
		kInfo,
		kWarning,
		kError
	};

	struct ModuleOutcomePresentation
	{
		std::string_view label;
		ModuleOutcomeSeverity severity;
	};

	[[nodiscard]] constexpr ModuleOutcomePresentation ClassifyModuleOutcome(
		ModuleOutcome a_outcome) noexcept
	{
		switch (a_outcome)
		{
		case ModuleOutcome::kInstalled:
			return { "Installed", ModuleOutcomeSeverity::kNormal };
		case ModuleOutcome::kDisabled:
			return { "Disabled", ModuleOutcomeSeverity::kDisabled };
		case ModuleOutcome::kSkipped:
			return { "Skipped", ModuleOutcomeSeverity::kWarning };
		case ModuleOutcome::kFailedQuery:
			return { "Failed query", ModuleOutcomeSeverity::kError };
		case ModuleOutcome::kFailedInstall:
			return { "Failed install", ModuleOutcomeSeverity::kError };
		case ModuleOutcome::kPending:
			return { "Pending", ModuleOutcomeSeverity::kInfo };
		}
		return { "Pending", ModuleOutcomeSeverity::kInfo };
	}

	enum class ModuleOutcomeFilter : uint8_t
	{
		kAll,
		kPending,
		kInstalled,
		kDisabled,
		kSkipped,
		kFailedQuery,
		kFailedInstall
	};

	struct ModuleOutcomeFilterOption
	{
		ModuleOutcomeFilter filter;
		std::string_view label;
	};

	inline constexpr std::array kModuleOutcomeFilters{
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kAll, "All outcomes" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kPending, "Pending" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kInstalled, "Installed" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kDisabled, "Disabled" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kSkipped, "Skipped" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kFailedQuery, "Failed query" },
		ModuleOutcomeFilterOption{ ModuleOutcomeFilter::kFailedInstall, "Failed install" }
	};

	[[nodiscard]] constexpr bool MatchesModuleStatus(
		std::string_view a_name,
		ModuleOutcome a_outcome,
		std::string_view a_search,
		ModuleOutcomeFilter a_filter) noexcept
	{
		ModuleOutcome selected{ ModuleOutcome::kPending };
		switch (a_filter)
		{
		case ModuleOutcomeFilter::kAll:
			break;
		case ModuleOutcomeFilter::kPending:
			selected = ModuleOutcome::kPending;
			break;
		case ModuleOutcomeFilter::kInstalled:
			selected = ModuleOutcome::kInstalled;
			break;
		case ModuleOutcomeFilter::kDisabled:
			selected = ModuleOutcome::kDisabled;
			break;
		case ModuleOutcomeFilter::kSkipped:
			selected = ModuleOutcome::kSkipped;
			break;
		case ModuleOutcomeFilter::kFailedQuery:
			selected = ModuleOutcome::kFailedQuery;
			break;
		case ModuleOutcomeFilter::kFailedInstall:
			selected = ModuleOutcome::kFailedInstall;
			break;
		}
		if (a_filter != ModuleOutcomeFilter::kAll && a_outcome != selected)
			return false;
		if (a_search.empty())
			return true;
		if (a_search.size() > a_name.size())
			return false;

		for (size_t offset = 0; offset + a_search.size() <= a_name.size(); ++offset)
		{
			auto matches = true;
			for (size_t index = 0; index < a_search.size(); ++index)
			{
				auto nameCharacter =
					static_cast<unsigned char>(a_name[offset + index]);
				auto searchCharacter =
					static_cast<unsigned char>(a_search[index]);
				if (nameCharacter >= 'A' && nameCharacter <= 'Z')
					nameCharacter = static_cast<unsigned char>(
						nameCharacter - 'A' + 'a');
				if (searchCharacter >= 'A' && searchCharacter <= 'Z')
					searchCharacter = static_cast<unsigned char>(
						searchCharacter - 'A' + 'a');
				if (nameCharacter != searchCharacter)
				{
					matches = false;
					break;
				}
			}
			if (matches)
				return true;
		}
		return false;
	}

	void DrawModulesPage(void* a_userData) noexcept;
}
