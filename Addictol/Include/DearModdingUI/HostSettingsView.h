#pragma once

#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/SettingsActions.h>

#include <optional>

namespace Addictol::DearModdingUI
{
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

	[[nodiscard]] bool HostSettingsTitleActionEnabled(
		SettingsAction a_action) noexcept;
	void InvokeHostSettingsTitleAction(
		SettingsAction a_action) noexcept;
	void DrawHostSettingsControls() noexcept;
}
