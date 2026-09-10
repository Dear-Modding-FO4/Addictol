#pragma once

#include <Core/Settings/AdSetting.h>

#include <string_view>

namespace Addictol::Menu
{
	[[nodiscard]] constexpr std::string_view SettingDisplayCategoryIconName(
		SettingDisplayCategory a_category) noexcept
	{
		switch (a_category)
		{
		case SettingDisplayCategory::kStability:
			return "shield-check";
		case SettingDisplayCategory::kPerformance:
			return "gauge";
		case SettingDisplayCategory::kVisuals:
			return "eye";
		case SettingDisplayCategory::kAudio:
			return "speaker-high";
		case SettingDisplayCategory::kGameplay:
			return "game-controller";
		case SettingDisplayCategory::kInterface:
			return "monitor";
		case SettingDisplayCategory::kDiagnostics:
			return "bug";
		case SettingDisplayCategory::kCount:
			break;
		}
		return {};
	}

	void BeginSettingsPageFrame() noexcept;
	void EndSettingsPageFrame(bool a_menuVisible) noexcept;
	void CloseSettingsPage() noexcept;
	[[nodiscard]] bool RegisterSettingsPage() noexcept;
}
