#include <DearModdingUI/FontCatalog.h>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace Addictol::DearModdingUI::FontCatalog
{
	namespace
	{
		[[nodiscard]] std::string Lower(std::string_view a_value)
		{
			std::string result{ a_value };
			std::ranges::transform(result, result.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::tolower(a_character));
			});
			return result;
		}

		[[nodiscard]] bool IsFontFile(
			const std::filesystem::path& a_path)
		{
			const auto extension = Lower(a_path.extension().string());
			return extension == ".ttf" ||
				extension == ".otf" ||
				extension == ".ttc";
		}

		[[nodiscard]] int CandidatePriority(
			const std::filesystem::path& a_path)
		{
			const auto stem = Lower(a_path.stem().string());
			if (stem.contains("regular"))
				return 0;
			constexpr std::string_view variants[]{
				"bold", "italic", "light", "medium", "semibold",
				"thin", "black", "icon", "fill"
			};
			for (const auto variant : variants)
			{
				if (stem.contains(variant))
					return 2;
			}
			return 1;
		}

		[[nodiscard]] bool SameName(
			std::string_view a_left,
			std::string_view a_right)
		{
			return Lower(a_left) == Lower(a_right);
		}
	}

	std::vector<FontFamily> Enumerate(
		const std::filesystem::path& a_root) noexcept
	{
		std::vector<FontFamily> result;
		std::error_code error;
		if (!std::filesystem::is_directory(a_root, error))
			return result;

		for (std::filesystem::directory_iterator iterator{ a_root, error }, end;
			iterator != end && !error;
			iterator.increment(error))
		{
			const auto& entry = *iterator;
			if (!entry.is_directory(error))
				continue;
			const auto familyName = entry.path().filename().string();
			if (SameName(familyName, "Phosphor"))
				continue;

			std::vector<std::filesystem::path> candidates;
			std::error_code familyError;
			for (std::filesystem::recursive_directory_iterator family{
					 entry.path(),
					 std::filesystem::directory_options::skip_permission_denied,
					 familyError
				 }, familyEnd;
				family != familyEnd && !familyError;
				family.increment(familyError))
			{
				if (family->is_regular_file(familyError) &&
					IsFontFile(family->path()))
					candidates.push_back(family->path());
			}
			if (candidates.empty())
				continue;

			std::ranges::sort(candidates, [](const auto& a_left, const auto& a_right) {
				return std::pair{
					CandidatePriority(a_left),
					Lower(a_left.filename().string())
				} <
					std::pair{
						CandidatePriority(a_right),
						Lower(a_right.filename().string())
					};
			});
			result.push_back({
				familyName,
				candidates.front().lexically_relative(a_root).generic_string()
			});
		}

		std::ranges::sort(result, [](const auto& a_left, const auto& a_right) {
			return Lower(a_left.name) < Lower(a_right.name);
		});
		return result;
	}

	const FontFamily* Resolve(
		std::string_view a_requested,
		const std::vector<FontFamily>& a_families,
		std::string_view a_fallback) noexcept
	{
		const auto find = [&](std::string_view a_name) {
			return std::ranges::find_if(a_families, [&](const auto& a_family) {
				return SameName(a_family.name, a_name);
			});
		};
		if (const auto requested = find(a_requested);
			requested != a_families.end())
			return std::addressof(*requested);
		if (const auto fallback = find(a_fallback);
			fallback != a_families.end())
			return std::addressof(*fallback);
		return a_families.empty() ? nullptr : std::addressof(a_families.front());
	}
}
