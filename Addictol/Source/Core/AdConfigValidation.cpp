#include <Core/AdConfigValidation.h>
#include <Core/Settings/AdSetting.h>
#include <REX/REX.h>
#include <toml11/single_include/toml.hpp>

namespace Addictol
{
	using namespace std::literals;

	bool IsKnownConfigSection(std::string_view a_section) noexcept
	{
		return SettingRegistry::GetSingleton().ContainsSection(a_section);
	}

	bool IsKnownConfigKey(
		std::string_view a_section,
		std::string_view a_key) noexcept
	{
		return SettingRegistry::GetSingleton().Find(a_section, a_key) != nullptr;
	}

	void ValidateConfigKeys(const char* a_filePath) noexcept
	{
		auto result = toml::try_parse(a_filePath);
		if (!result.is_ok())
			return;

		auto& data = result.unwrap();
		if (!data.is_table())
			return;

		for (auto& [sectionName, sectionValue] : data.as_table())
		{
			if (!IsKnownConfigSection(sectionName))
			{
				REX::WARN("Config: unknown section [{}] in \"{}\""sv, sectionName, a_filePath);
				continue;
			}

			if (!sectionValue.is_table())
				continue;

			for (auto& [keyName, keyValue] : sectionValue.as_table())
			{
				if (!IsKnownConfigKey(sectionName, keyName))
					REX::WARN("Config: unknown key \"{}\" in [{}] in \"{}\""sv, keyName, sectionName, a_filePath);
			}
		}
	}
}
