#pragma once

#include <Core/Settings/AdSetting.h>

#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Addictol
{
	inline constexpr std::string_view kAddictolSettingsDirectory{ "Data/F4SE/Plugins" };
	inline constexpr std::string_view kAddictolSettingsFileName{ "Addictol.toml" };
	inline constexpr std::string_view kAddictolCustomSettingsFileName{ "AddictolCustom.toml" };

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
	[[nodiscard]] bool BuildSettingsDocumentToml(
		std::string_view a_existingToml,
		std::string& a_output,
		std::string& a_error) noexcept;
	[[nodiscard]] bool WriteSettingsOverrideFile(
		const std::filesystem::path& a_path,
		std::span<const SettingValueSnapshot> a_settings,
		std::string& a_error,
		bool* a_changed = nullptr) noexcept;
	[[nodiscard]] bool RefreshSettingsDocument(
		const std::filesystem::path& a_path,
		std::string& a_error,
		bool* a_changed = nullptr) noexcept;
	[[nodiscard]] std::filesystem::path ResolveSettingsPath(
		const std::filesystem::path& a_directory) noexcept;
	[[nodiscard]] std::filesystem::path ResolveSettingsPath() noexcept;
	void InitializeSettings() noexcept;
	void InitializeSettings(const std::filesystem::path& a_path) noexcept;

	class SettingsRepository
	{
	public:
		[[nodiscard]] static SettingsRepository& GetSingleton() noexcept;
		explicit SettingsRepository(std::filesystem::path a_path);

		[[nodiscard]] std::vector<SettingValueSnapshot> Snapshot() const;
		[[nodiscard]] SettingsApplyResult Apply(
			std::span<const SettingValueSnapshot> a_settings) noexcept;

	private:
		std::filesystem::path m_path;
		mutable std::mutex m_mutex;
		std::vector<SettingValueSnapshot> m_committed;
	};
}
