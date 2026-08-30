#pragma once

#include <DearModdingUI/HostSettings.h>

#include <cstdint>
#include <optional>

namespace Addictol::DearModdingUI
{
	enum class HostSettingsTitleAction : uint32_t
	{
		kApply,
		kRevert,
		kReset
	};

	struct HostSettingsTitleActionAvailability
	{
		bool apply{ false };
		bool revert{ false };
		bool reset{ true };
	};

	[[nodiscard]] constexpr HostSettingsTitleActionAvailability
		ResolveHostSettingsTitleActionAvailability(bool a_dirty) noexcept
	{
		return { a_dirty, a_dirty, true };
	}

	struct HostSettingsDraftState
	{
		HostInterfaceSettings committed;
		HostInterfaceSettings draft;
		bool active{ false };
	};

	struct HostSettingsDraftCommit
	{
		std::optional<HostInterfaceSettings> settings;
	};

	[[nodiscard]] inline HostSettingsDraftState BeginHostSettingsDraft(
		const HostInterfaceSettings& a_committed)
	{
		return { a_committed, a_committed, true };
	}

	[[nodiscard]] inline bool HostSettingsDraftDiffers(
		const HostSettingsDraftState& a_state) noexcept
	{
		return a_state.active && a_state.draft != a_state.committed;
	}

	[[nodiscard]] inline bool HostSettingsDraftRequiresAtlasRebuild(
		const HostInterfaceSettings& a_committed,
		const HostInterfaceSettings& a_draft) noexcept
	{
		return a_draft.uiScale != a_committed.uiScale ||
			a_draft.bodyFontFamily != a_committed.bodyFontFamily;
	}

	[[nodiscard]] inline HostSettingsDraftCommit ApplyHostSettingsDraft(
		HostSettingsDraftState& a_state)
	{
		if (!HostSettingsDraftDiffers(a_state))
			return {};

		a_state.committed = a_state.draft;
		return { a_state.committed };
	}

	inline void RevertHostSettingsDraft(
		HostSettingsDraftState& a_state)
	{
		if (a_state.active)
			a_state.draft = a_state.committed;
	}

	inline void ResetHostSettingsDraft(
		HostSettingsDraftState& a_state)
	{
		if (a_state.active)
			a_state.draft = DefaultHostInterfaceSettings();
	}

	inline void LeaveHostSettingsDraft(
		HostSettingsDraftState& a_state)
	{
		RevertHostSettingsDraft(a_state);
		a_state.active = false;
	}

	[[nodiscard]] HostSettingsTitleActionAvailability
		GetHostSettingsTitleActionAvailability() noexcept;
	void InvokeHostSettingsTitleAction(
		HostSettingsTitleAction a_action) noexcept;
	void DrawHostSettingsControls() noexcept;
}
