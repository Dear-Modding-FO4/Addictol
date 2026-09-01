#pragma once

#include <Core/Settings/AdSettingPersistence.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol
{
	inline constexpr std::array kSettingDisplayCategoryOrder{
		SettingDisplayCategory::kStability,
		SettingDisplayCategory::kPerformance,
		SettingDisplayCategory::kVisuals,
		SettingDisplayCategory::kAudio,
		SettingDisplayCategory::kGameplay,
		SettingDisplayCategory::kInterface,
		SettingDisplayCategory::kDiagnostics
	};

	enum class SettingControlKind : uint8_t
	{
		kCheckbox,
		kSlider,
		kDrag,
		kNumericInput,
		kTextInput,
		kCombo
	};

	struct SettingIdentity
	{
		std::string section;
		std::string key;

		bool operator==(const SettingIdentity&) const noexcept = default;
	};

	struct SettingDraftEntry
	{
		const SettingEntry* setting;
		SettingValue committed;
		SettingValue draft;
	};

	struct SettingsDraftState
	{
		std::vector<SettingDraftEntry> entries;
		bool active{ false };
	};

	struct SettingsDraftCommit
	{
		std::vector<SettingValueSnapshot> values;
		std::vector<size_t> changedIndices;
	};

	[[nodiscard]] std::string_view SettingDisplayCategoryName(
		SettingDisplayCategory a_category) noexcept;
	[[nodiscard]] std::span<const std::string_view> SettingStringOptions(
		const SettingEntry& a_setting) noexcept;
	[[nodiscard]] SettingControlKind SelectSettingControl(
		const SettingEntry& a_setting) noexcept;
	[[nodiscard]] bool IsSettingModified(
		const SettingEntry& a_setting,
		const SettingValue& a_value) noexcept;
	[[nodiscard]] SettingIdentity MakeSettingIdentity(
		const SettingEntry& a_setting);
	[[nodiscard]] SettingDraftEntry* ResolveSettingDraftEntry(
		SettingsDraftState& a_state,
		const SettingIdentity& a_identity) noexcept;
	[[nodiscard]] const SettingDraftEntry* ResolveSettingDraftEntry(
		const SettingsDraftState& a_state,
		const SettingIdentity& a_identity) noexcept;
	[[nodiscard]] SettingValue NormalizeSettingDraftValue(
		const SettingEntry& a_setting,
		SettingValue a_value);

	[[nodiscard]] SettingsDraftState BeginSettingsDraft(
		std::span<const SettingValueSnapshot> a_committed);
	[[nodiscard]] bool SettingsDraftDiffers(
		const SettingsDraftState& a_state) noexcept;
	[[nodiscard]] size_t SettingsDraftPendingCount(
		const SettingsDraftState& a_state) noexcept;
	[[nodiscard]] SettingsDraftCommit PrepareSettingsDraftApply(
		const SettingsDraftState& a_state);
	void CompleteSettingsDraftApply(
		SettingsDraftState& a_state,
		const SettingsDraftCommit& a_commit);
	void RevertSettingsDraft(SettingsDraftState& a_state);
	void ResetSettingsDraftToDefaults(SettingsDraftState& a_state);
	void LeaveSettingsDraft(SettingsDraftState& a_state);
}
