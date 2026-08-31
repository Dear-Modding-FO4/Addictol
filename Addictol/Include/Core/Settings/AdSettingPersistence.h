#pragma once

#include <Core/Settings/AdSetting.h>

#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Addictol
{
	inline constexpr std::string_view kAddictolCustomSettingsPath{
		"Data/F4SE/Plugins/AddictolCustom.toml"
	};

	struct SettingValueSnapshot
	{
		const SettingEntry* setting;
		SettingValue value;
	};

	struct SettingsApplyResult
	{
		bool success{ false };
		size_t changed{ 0 };
		std::string error;
	};

	[[nodiscard]] bool BuildSettingsOverrideToml(
		std::string_view a_existingToml,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_output,
		std::string& a_error) noexcept;
	[[nodiscard]] bool WriteSettingsOverrideFile(
		const std::filesystem::path& a_path,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_error) noexcept;

	class SettingsRepository
	{
	public:
		[[nodiscard]] static SettingsRepository& GetSingleton() noexcept;

		[[nodiscard]] std::vector<SettingValueSnapshot> Snapshot() const;
		[[nodiscard]] SettingsApplyResult Apply(
			std::span<const SettingValueSnapshot> a_settings) noexcept;

	private:
		SettingsRepository();

		mutable std::mutex m_mutex;
		std::vector<SettingValueSnapshot> m_committed;
	};
}
