#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Addictol
{
	enum class ModuleOutcome : uint8_t
	{
		kPending,
		kInstalled,
		kDisabled,
		kSkipped,
		kFailedQuery,
		kFailedInstall
	};

	using ModuleOutcomeTally = std::array<uint64_t, 5>;

	constexpr void RecordModuleOutcome(
		ModuleOutcome& a_current,
		ModuleOutcomeTally& a_tally,
		ModuleOutcome a_next) noexcept
	{
		const auto update = [&a_tally](ModuleOutcome a_outcome, bool a_increment) {
			auto* count = [&]() -> uint64_t* {
				switch (a_outcome)
				{
				case ModuleOutcome::kInstalled:
					return &a_tally[0];
				case ModuleOutcome::kDisabled:
					return &a_tally[1];
				case ModuleOutcome::kSkipped:
					return &a_tally[2];
				case ModuleOutcome::kFailedQuery:
					return &a_tally[3];
				case ModuleOutcome::kFailedInstall:
					return &a_tally[4];
				case ModuleOutcome::kPending:
					return nullptr;
				}
				return nullptr;
			}();
			if (!count)
				return;
			if (a_increment)
				++*count;
			else if (*count > 0)
				--*count;
		};

		update(a_current, false);
		a_current = a_next;
		update(a_current, true);
	}

	template <class Range>
	[[nodiscard]] constexpr ModuleOutcomeTally TallyModuleOutcomes(
		const Range& a_modules) noexcept
	{
		ModuleOutcomeTally tally{};
		for (const auto& module : a_modules)
		{
			switch (module.outcome)
			{
			case ModuleOutcome::kInstalled:
				++tally[0];
				break;
			case ModuleOutcome::kDisabled:
				++tally[1];
				break;
			case ModuleOutcome::kSkipped:
				++tally[2];
				break;
			case ModuleOutcome::kFailedQuery:
				++tally[3];
				break;
			case ModuleOutcome::kFailedInstall:
				++tally[4];
				break;
			case ModuleOutcome::kPending:
				break;
			}
		}
		return tally;
	}
}
