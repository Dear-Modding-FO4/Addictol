#include <Core/Settings/AdSettingsModel.h>

#include <algorithm>
#include <array>
#include <cctype>

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
		inline constexpr std::array<std::string_view, 16> kMenuToggleKeyOptions{
			"F1",
			"F2",
			"F3",
			"F4",
			"F5",
			"F6",
			"F7",
			"F8",
			"F9",
			"F10",
			"F11",
			"F12",
			"Home",
			"End",
			"Insert",
			"Delete"
		};

		[[nodiscard]] std::string Lower(std::string_view a_value)
		{
			std::string result{ a_value };
			std::ranges::transform(
				result,
				result.begin(),
				[](unsigned char a_character) {
					return static_cast<char>(std::tolower(a_character));
				});
			return result;
		}

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

	bool IsHostPresentationSetting(const SettingEntry& a_setting) noexcept
	{
		if (a_setting.Section() != "Additional")
			return false;
		constexpr std::array keys{
			std::string_view{ "bMenuMonochromeIcons" },
			std::string_view{ "sMenuAccentColor" },
			std::string_view{ "fMenuWindowOpacity" },
			std::string_view{ "bMenuBackgroundBlur" },
			std::string_view{ "fMenuBackgroundBlurStrength" },
			std::string_view{ "fMenuUiScale" },
			std::string_view{ "sMenuBodyFontFamily" }
		};
		return std::ranges::find(keys, a_setting.Key()) != keys.end();
	}

	bool IsSettingsPageEditable(const SettingEntry& a_setting) noexcept
	{
		return !IsHostPresentationSetting(a_setting);
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
		if (IsSettingKey(a_setting, "sMenuToggleKey"))
			return kMenuToggleKeyOptions;
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

	bool MatchesSettingFilter(
		const SettingEntry& a_setting,
		const SettingValue& a_value,
		const SettingFilter& a_filter)
	{
		if (a_filter.modifiedOnly &&
			!IsSettingModified(a_setting, a_value))
			return false;
		if (a_filter.search.empty())
			return true;

		const auto search = Lower(a_filter.search);
		return Lower(a_setting.Key()).contains(search) ||
			Lower(a_setting.DisplayName()).contains(search) ||
			Lower(a_setting.Description()).contains(search);
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
		{
			if (IsSettingsPageEditable(*entry.setting))
				entry.draft = entry.setting->DefaultValue();
		}
	}

	void ResetSettingDraft(SettingDraftEntry& a_entry)
	{
		if (IsSettingsPageEditable(*a_entry.setting))
			a_entry.draft = a_entry.setting->DefaultValue();
	}

	void LeaveSettingsDraft(SettingsDraftState& a_state)
	{
		RevertSettingsDraft(a_state);
		a_state.active = false;
	}
}
