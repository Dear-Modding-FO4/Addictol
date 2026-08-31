#pragma once

#include <string_view>

namespace Addictol
{
	[[nodiscard]] bool IsKnownConfigSection(std::string_view a_section) noexcept;
	[[nodiscard]] bool IsKnownConfigKey(
		std::string_view a_section,
		std::string_view a_key) noexcept;
	void ValidateConfigKeys(const char* a_filePath) noexcept;
}
