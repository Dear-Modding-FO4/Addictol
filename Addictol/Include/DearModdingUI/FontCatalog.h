#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol::DearModdingUI::FontCatalog
{
	struct FontFamily
	{
		std::string name;
		std::string regularFile;

		[[nodiscard]] bool operator==(const FontFamily&) const noexcept = default;
	};

	[[nodiscard]] std::vector<FontFamily> Enumerate(
		const std::filesystem::path& a_root) noexcept;
	[[nodiscard]] const FontFamily* Resolve(
		std::string_view a_requested,
		const std::vector<FontFamily>& a_families,
		std::string_view a_fallback) noexcept;
}
