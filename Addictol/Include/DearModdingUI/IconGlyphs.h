#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI
{
	enum class IconKind : uint32_t
	{
		kCategory,
		kClient
	};

	namespace PhosphorGlyph
	{
		inline constexpr char32_t kFirstPrivateUse{ 0xE000 };
		inline constexpr char32_t kLastPrivateUse{ 0xEE82 };

		// Generated from @phosphor-icons/web 2.1.2 Fill selection.json.
		inline constexpr char32_t kQuestion{ 0xE3E8 };
		inline constexpr char32_t kArchive{ 0xE00C };
		inline constexpr char32_t kAppWindow{ 0xE5DA };
		inline constexpr char32_t kArrowCounterClockwise{ 0xE038 };
		inline constexpr char32_t kArrowsClockwise{ 0xE094 };
		inline constexpr char32_t kClipboardText{ 0xE198 };
		inline constexpr char32_t kDotsThreeCircle{ 0xE200 };
		inline constexpr char32_t kFloppyDisk{ 0xE248 };
		inline constexpr char32_t kGear{ 0xE270 };
		inline constexpr char32_t kGameController{ 0xE26E };
		inline constexpr char32_t kMonitor{ 0xE32E };
		inline constexpr char32_t kPalette{ 0xE6C8 };
		inline constexpr char32_t kShieldCheck{ 0xE40C };
		inline constexpr char32_t kSpeakerHigh{ 0xE44A };
		inline constexpr char32_t kSun{ 0xE472 };
		inline constexpr char32_t kTrash{ 0xE4A6 };
		inline constexpr char32_t kX{ 0xE4F6 };
		inline constexpr char32_t kPuzzlePiece{ 0xE596 };
		inline constexpr char32_t kGauge{ 0xE628 };
		inline constexpr char32_t kMagicWand{ 0xE6B6 };
		inline constexpr char32_t kTerminalWindow{ 0xEAE8 };
		inline constexpr char32_t kSquaresFour{ 0xE464 };
		inline constexpr char32_t kFiles{ 0xE710 };
	}

	struct IconGlyphMapping
	{
		std::string_view slug;
		char32_t glyph;
	};

	inline constexpr std::array kCategoryGlyphs{
		IconGlyphMapping{ "compatibility", PhosphorGlyph::kPuzzlePiece },
		IconGlyphMapping{ "dev-tools", PhosphorGlyph::kTerminalWindow },
		IconGlyphMapping{ "diagnostics", PhosphorGlyph::kTerminalWindow },
		IconGlyphMapping{ "audio", PhosphorGlyph::kSpeakerHigh },
		IconGlyphMapping{ "gameplay", PhosphorGlyph::kGameController },
		IconGlyphMapping{ "interface", PhosphorGlyph::kMonitor },
		IconGlyphMapping{ "lighting", PhosphorGlyph::kSun },
		IconGlyphMapping{ "misc", PhosphorGlyph::kDotsThreeCircle },
		IconGlyphMapping{ "other", PhosphorGlyph::kDotsThreeCircle },
		IconGlyphMapping{ "overlay", PhosphorGlyph::kAppWindow },
		IconGlyphMapping{ "performance", PhosphorGlyph::kGauge },
		IconGlyphMapping{ "post-process", PhosphorGlyph::kMagicWand },
		IconGlyphMapping{ "postprocess", PhosphorGlyph::kMagicWand },
		IconGlyphMapping{ "stability", PhosphorGlyph::kShieldCheck },
		IconGlyphMapping{ "visuals", PhosphorGlyph::kPalette },
		IconGlyphMapping{ "unloaded", PhosphorGlyph::kArchive }
	};

	inline constexpr std::array kClientGlyphs{
		IconGlyphMapping{ "dearmoddingaddictol", PhosphorGlyph::kPuzzlePiece },
		IconGlyphMapping{ "dearmoddingcommunityshaders", PhosphorGlyph::kSun }
	};

	inline constexpr std::array kActionGlyphs{
		IconGlyphMapping{ "arrowcounterclockwise", PhosphorGlyph::kArrowCounterClockwise },
		IconGlyphMapping{ "arrow-counter-clockwise", PhosphorGlyph::kArrowCounterClockwise },
		IconGlyphMapping{ "arrowsclockwise", PhosphorGlyph::kArrowsClockwise },
		IconGlyphMapping{ "arrows-clockwise", PhosphorGlyph::kArrowsClockwise },
		IconGlyphMapping{ "clearcache", PhosphorGlyph::kTrash },
		IconGlyphMapping{ "clear-cache", PhosphorGlyph::kTrash },
		IconGlyphMapping{ "clipboard", PhosphorGlyph::kClipboardText },
		IconGlyphMapping{ "clipboardtext", PhosphorGlyph::kClipboardText },
		IconGlyphMapping{ "clipboard-text", PhosphorGlyph::kClipboardText },
		IconGlyphMapping{ "floppydisk", PhosphorGlyph::kFloppyDisk },
		IconGlyphMapping{ "floppy-disk", PhosphorGlyph::kFloppyDisk },
		IconGlyphMapping{ "restoresettings", PhosphorGlyph::kArrowCounterClockwise },
		IconGlyphMapping{ "restore-settings", PhosphorGlyph::kArrowCounterClockwise },
		IconGlyphMapping{ "trash", PhosphorGlyph::kTrash }
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

	[[nodiscard]] inline std::string NormalizeIconOwnerName(std::string_view a_name)
	{
		std::string normalized;
		normalized.reserve(a_name.size());
		for (const auto value : a_name)
		{
			const auto character = static_cast<unsigned char>(value);
			if ((character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9'))
			{
				normalized.push_back(static_cast<char>(character));
			}
			else if (character >= 'A' && character <= 'Z')
			{
				normalized.push_back(static_cast<char>(character - 'A' + 'a'));
			}
		}
		return normalized;
	}

	template <std::size_t Size>
	[[nodiscard]] inline char32_t FindIconGlyph(
		const std::array<IconGlyphMapping, Size>& a_mappings,
		std::string_view a_name)
	{
		const auto slug = SlugifyIconName(a_name);
		for (const auto& mapping : a_mappings)
		{
			if (mapping.slug == slug)
				return mapping.glyph;
		}
		return PhosphorGlyph::kQuestion;
	}

	[[nodiscard]] inline char32_t ResolveIconGlyph(
		IconKind a_kind,
		std::string_view a_name) noexcept
	{
		try
		{
			return a_kind == IconKind::kCategory ?
				FindIconGlyph(kCategoryGlyphs, a_name) :
				FindIconGlyph(kClientGlyphs, a_name);
		}
		catch (...)
		{
			return static_cast<char32_t>(PhosphorGlyph::kQuestion);
		}
	}

	[[nodiscard]] inline char32_t ResolveCategoryIconGlyph(
		std::string_view a_category,
		std::string_view a_clientDisplayName,
		std::string_view a_clientId) noexcept
	{
		try
		{
			const auto category = NormalizeIconOwnerName(a_category);
			if (!category.empty() &&
				(category == NormalizeIconOwnerName(a_clientDisplayName) ||
					category == NormalizeIconOwnerName(a_clientId)))
				return FindIconGlyph(kClientGlyphs, a_clientId);
			return FindIconGlyph(kCategoryGlyphs, a_category);
		}
		catch (...)
		{
			return static_cast<char32_t>(PhosphorGlyph::kQuestion);
		}
	}

	[[nodiscard]] inline char32_t ResolveActionIconGlyph(
		std::string_view a_name) noexcept
	{
		try
		{
			const auto slug = SlugifyIconName(a_name);
			for (const auto& mapping : kActionGlyphs)
			{
				if (mapping.slug == slug)
					return mapping.glyph;
			}
			return {};
		}
		catch (...)
		{
			return {};
		}
	}
}
