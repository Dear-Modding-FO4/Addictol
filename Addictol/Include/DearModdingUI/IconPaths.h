#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Addictol::DearModdingUI
{
	enum class IconKind
	{
		kCategory,
		kClient
	};

	[[nodiscard]] inline std::string SlugifyIconName(std::string_view a_name)
	{
		std::string slug;
		slug.reserve(a_name.size());
		bool separatorPending = false;
		for (const auto value : a_name)
		{
			const auto character = static_cast<unsigned char>(value);
			if ((character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9'))
			{
				if (separatorPending && !slug.empty())
					slug.push_back('-');
				slug.push_back(static_cast<char>(character));
				separatorPending = false;
			}
			else if (character >= 'A' && character <= 'Z')
			{
				if (separatorPending && !slug.empty())
					slug.push_back('-');
				slug.push_back(static_cast<char>(character - 'A' + 'a'));
				separatorPending = false;
			}
			else if (character == ' ' || character == '_')
			{
				separatorPending = !slug.empty();
			}
		}
		return slug;
	}

	[[nodiscard]] inline std::optional<std::filesystem::path> BuildIconPath(
		const std::filesystem::path& a_root,
		IconKind a_kind,
		std::string_view a_name)
	{
		auto slug = SlugifyIconName(a_name);
		if (slug.empty())
			return std::nullopt;
		const auto folder = a_kind == IconKind::kCategory ? "Categories" : "Clients";
		return a_root / folder / (std::move(slug) + ".png");
	}

	template <class Exists>
	[[nodiscard]] std::optional<std::filesystem::path> ResolveIconPath(
		const std::filesystem::path& a_root,
		IconKind a_kind,
		std::string_view a_name,
		Exists&& a_exists)
	{
		auto path = BuildIconPath(a_root, a_kind, a_name);
		return path && a_exists(*path) ? std::move(path) : std::nullopt;
	}
}
