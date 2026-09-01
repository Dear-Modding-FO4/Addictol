#include <Core/Settings/AdSettingsModel.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace Addictol
{
	namespace
	{
		inline constexpr std::array<std::string_view, 1> kAllocatorOptions{
			"voltek"
		};
		inline constexpr std::array<std::string_view, 2> kZlibOptions{
			"libdeflate",
			"stock"
		};
		inline constexpr std::array<std::string_view, 7> kLogLevelOptions{
			"trace",
			"debug",
			"info",
			"warn",
			"error",
			"critical",
			"off"
		};
		[[nodiscard]] bool IsSettingKey(
			const SettingEntry& a_setting,
			std::string_view a_key) noexcept
		{
			return a_setting.Section() == "Additional" &&
				a_setting.Key() == a_key;
		}
	}

	std::string_view SettingDisplayCategoryName(
		SettingDisplayCategory a_category) noexcept
	{
		switch (a_category)
		{
		case SettingDisplayCategory::kStability:
			return "Stability";
		case SettingDisplayCategory::kPerformance:
			return "Performance";
		case SettingDisplayCategory::kVisuals:
			return "Visuals";
		case SettingDisplayCategory::kAudio:
			return "Audio";
		case SettingDisplayCategory::kGameplay:
			return "Gameplay";
		case SettingDisplayCategory::kInterface:
			return "Interface";
		case SettingDisplayCategory::kDiagnostics:
			return "Diagnostics";
		default:
			return {};
		}
	}

	std::span<const std::string_view> SettingStringOptions(
		const SettingEntry& a_setting) noexcept
	{
		if (IsSettingKey(a_setting, "sAllocator"))
			return kAllocatorOptions;
		if (IsSettingKey(a_setting, "sZlibBackend"))
			return kZlibOptions;
		if (IsSettingKey(a_setting, "sLogLevel") ||
			IsSettingKey(a_setting, "sLogFlushLevel"))
			return kLogLevelOptions;
		return {};
	}

	SettingControlKind SelectSettingControl(
		const SettingEntry& a_setting) noexcept
	{
		switch (a_setting.Type())
		{
		case SettingValueType::kBoolean:
			return SettingControlKind::kCheckbox;
		case SettingValueType::kString:
			return SettingStringOptions(a_setting).empty() ?
				SettingControlKind::kTextInput :
				SettingControlKind::kCombo;
		default:
			if (!a_setting.NumericRange())
				return SettingControlKind::kNumericInput;
			if (a_setting.NumericRange()->minimum &&
				a_setting.NumericRange()->maximum)
				return SettingControlKind::kSlider;
			return SettingControlKind::kDrag;
		}
	}

	bool IsSettingModified(
		const SettingEntry& a_setting,
		const SettingValue& a_value) noexcept
	{
		return a_value != a_setting.DefaultValue();
	}

	SettingIdentity MakeSettingIdentity(const SettingEntry& a_setting)
	{
		return {
			std::string{ a_setting.Section() },
			std::string{ a_setting.Key() }
		};
	}

	SettingDraftEntry* ResolveSettingDraftEntry(
		SettingsDraftState& a_state,
		const SettingIdentity& a_identity) noexcept
	{
		if (!a_state.active)
			return nullptr;
		const auto entry = std::ranges::find_if(
			a_state.entries,
			[&](const SettingDraftEntry& a_entry) {
				return a_entry.setting->Section() == a_identity.section &&
					a_entry.setting->Key() == a_identity.key;
			});
		return entry == a_state.entries.end() ? nullptr : &*entry;
	}

	const SettingDraftEntry* ResolveSettingDraftEntry(
		const SettingsDraftState& a_state,
		const SettingIdentity& a_identity) noexcept
	{
		if (!a_state.active)
			return nullptr;
		const auto entry = std::ranges::find_if(
			a_state.entries,
			[&](const SettingDraftEntry& a_entry) {
				return a_entry.setting->Section() == a_identity.section &&
					a_entry.setting->Key() == a_identity.key;
			});
		return entry == a_state.entries.end() ? nullptr : &*entry;
	}

	SettingValue NormalizeSettingDraftValue(
		const SettingEntry& a_setting,
		SettingValue a_value)
	{
		const auto validType =
			(a_setting.Type() == SettingValueType::kBoolean &&
				std::holds_alternative<bool>(a_value)) ||
			(a_setting.Type() == SettingValueType::kFloat32 &&
				std::holds_alternative<double>(a_value)) ||
			(a_setting.Type() == SettingValueType::kInt32 &&
				std::holds_alternative<int64_t>(a_value)) ||
			(a_setting.Type() == SettingValueType::kUInt32 &&
				std::holds_alternative<uint64_t>(a_value)) ||
			(a_setting.Type() == SettingValueType::kString &&
				std::holds_alternative<std::string>(a_value));
		if (!validType)
			return a_setting.DefaultValue();

		const auto range = a_setting.NumericRange();
		std::visit(
			[&](auto& a_typedValue) {
				using T = std::remove_cvref_t<decltype(a_typedValue)>;
				if constexpr (
					std::is_same_v<T, double> ||
					std::is_same_v<T, int64_t> ||
					std::is_same_v<T, uint64_t>)
				{
					auto number = static_cast<double>(a_typedValue);
					if constexpr (std::is_same_v<T, double>)
					{
						if (!std::isfinite(number))
							number = std::get<double>(a_setting.DefaultValue());
						number = std::clamp(
							number,
							-static_cast<double>(
								(std::numeric_limits<float>::max)()),
							static_cast<double>(
								(std::numeric_limits<float>::max)()));
					}
					else if constexpr (std::is_same_v<T, int64_t>)
					{
						number = std::clamp(
							number,
							static_cast<double>(
								(std::numeric_limits<int32_t>::min)()),
							static_cast<double>(
								(std::numeric_limits<int32_t>::max)()));
					}
					else
					{
						number = (std::min)(
							number,
							static_cast<double>(
								(std::numeric_limits<uint32_t>::max)()));
					}
					if (range && range->minimum)
						number = (std::max)(number, *range->minimum);
					if (range && range->maximum)
						number = (std::min)(number, *range->maximum);
					a_typedValue = static_cast<T>(number);
				}
			},
			a_value);
		return a_value;
	}

	SettingsDraftState BeginSettingsDraft(
		std::span<const SettingValueSnapshot> a_committed)
	{
		SettingsDraftState state;
		state.entries.reserve(a_committed.size());
		for (const auto& item : a_committed)
		{
			state.entries.push_back({
				item.setting,
				item.value,
				item.value
			});
		}
		state.active = true;
		return state;
	}

	bool SettingsDraftDiffers(
		const SettingsDraftState& a_state) noexcept
	{
		return SettingsDraftPendingCount(a_state) != 0;
	}

	size_t SettingsDraftPendingCount(
		const SettingsDraftState& a_state) noexcept
	{
		if (!a_state.active)
			return 0;
		return static_cast<size_t>(std::ranges::count_if(
			a_state.entries,
			[](const SettingDraftEntry& a_entry) {
				return a_entry.draft != a_entry.committed;
			}));
	}

	SettingsDraftCommit PrepareSettingsDraftApply(
		const SettingsDraftState& a_state)
	{
		SettingsDraftCommit commit;
		if (!a_state.active)
			return commit;
		commit.values.reserve(a_state.entries.size());
		for (size_t index = 0; index < a_state.entries.size(); ++index)
		{
			const auto& entry = a_state.entries[index];
			commit.values.push_back({ entry.setting, entry.draft });
			if (entry.draft != entry.committed)
				commit.changedIndices.push_back(index);
		}
		return commit;
	}

	void CompleteSettingsDraftApply(
		SettingsDraftState& a_state,
		const SettingsDraftCommit& a_commit)
	{
		if (!a_state.active)
			return;
		for (const auto index : a_commit.changedIndices)
		{
			if (index < a_state.entries.size())
				a_state.entries[index].committed =
					a_state.entries[index].draft;
		}
	}

	void RevertSettingsDraft(SettingsDraftState& a_state)
	{
		if (!a_state.active)
			return;
		for (auto& entry : a_state.entries)
			entry.draft = entry.committed;
	}

	void ResetSettingsDraftToDefaults(SettingsDraftState& a_state)
	{
		if (!a_state.active)
			return;
		for (auto& entry : a_state.entries)
			entry.draft = entry.setting->DefaultValue();
	}

	void LeaveSettingsDraft(SettingsDraftState& a_state)
	{
		RevertSettingsDraft(a_state);
		a_state.active = false;
	}
}
