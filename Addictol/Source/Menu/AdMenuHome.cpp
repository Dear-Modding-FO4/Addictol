#include <Menu/AdMenuHome.h>

#include <Core/AdPlugin.h>
#include <Menu/AdMenu.h>

#include <DearModdingUI/UI.h>

#include <array>
#include <cstdio>
#include <cstdint>

namespace Addictol::Menu
{
	namespace
	{
		constexpr dmui::TextStyle kProseText{
			.fontRole = DMUI_FONT_ROLE_SUBTEXT,
			.wrapped = true
		};

		constexpr std::array kFaqEntries{
			dmui::FaqEntry{
				"Which Fallout 4 runtimes are supported?",
				"One Addictol DLL supports OG 1.10.163, NG 1.10.984, and AE 1.11.240." },
			dmui::FaqEntry{
				"Where is the configuration stored?",
				"Defaults are documented in Data/F4SE/Plugins/Addictol.toml. Put overrides in AddictolCustom.toml beside it so updates do not overwrite them." }
		};

		struct ModCheck
		{
			const char* check;
			const char* detail;
		};

		constexpr std::array<ModCheck, 6> kModChecks{
			ModCheck{
				"Is the source public?",
				"If it links CommonLibF4, GPL-3.0 requires it." },
			ModCheck{
				"Do the claims come with data?",
				"\"+30% FPS\" should arrive with a method and numbers you can reproduce." },
			ModCheck{
				"How does the author handle bug reports?",
				"Engagement and fixes, or deletion and blocking." },
			ModCheck{
				"Does it duplicate a fix you already have?",
				"Two mods patching the same code is a common cause of crashes." },
			ModCheck{
				"Does the file list make sense?",
				"Stray DLLs, unexplained INIs, and bundled redistributables deserve a question." },
			ModCheck{
				"Does it say what it actually changes?",
				"A changelog naming specific systems beats \"various optimizations.\"" }
		};

		void DrawWelcomeSection() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "Welcome to Addictol", { .fontRole = DMUI_FONT_ROLE_TITLE }));
			dmui::ui::Spacing();
			ReportPresentationResult(dmui::DrawStyledText(
				Client(),
				"Addictol combines engine fixes, crash fixes, and performance patches "
				"for Fallout 4 in a single F4SE plugin. Use the pages on the left to "
				"inspect live diagnostics and runtime behavior.",
				kProseText));
			dmui::ui::Spacing();
			ReportPresentationResult(Client().DrawSectionHeader(
				"Overview",
				DearModdingUI::FindPhosphorSlugGlyphOrZero("info")));

			const auto counts =
				Plugin::GetSingleton()->GetModules().ModuleOutcomeCounts();
			const auto total =
				counts[0] + counts[1] + counts[2] + counts[3] + counts[4];
			static const auto runtime =
				REX::FModule::GetExecutingModule().GetFileVersion();
			dmui::ui::Text(
				"Game runtime: %u.%u.%u.%u",
				runtime.major(),
				runtime.minor(),
				runtime.patch(),
				runtime.build());
			dmui::ui::Text(
				"Modules: %llu installed, %llu disabled, %llu skipped",
				static_cast<unsigned long long>(counts[0]),
				static_cast<unsigned long long>(counts[1]),
				static_cast<unsigned long long>(counts[2]));
			dmui::ui::TextColored(
				counts[3] == 0 && counts[4] == 0 ?
					dmui::ToUIVec4(ThemeColors().statusSuccess) :
					dmui::ToUIVec4(ThemeColors().statusError),
				"%llu failed query, %llu failed install (%llu total)",
				static_cast<unsigned long long>(counts[3]),
				static_cast<unsigned long long>(counts[4]),
				static_cast<unsigned long long>(total));
			dmui::ui::Spacing();
		}

		void DrawQuickLinksSection() noexcept
		{
			ReportPresentationResult(Client().DrawSectionHeader(
				"Quick Links",
				DearModdingUI::FindPhosphorSlugGlyphOrZero("link")));
			if (!Client().DrawLinkRow("Addictol.QuickLinks", kHomeQuickLinks))
			{
				const auto result = Client().LastResult();
				ReportStatus(
					DMUI_STATUS_SEVERITY_ERROR,
					"A project link failed. See Addictol.log for details.");
				REX::WARN(
					"Menu: project link row failed, result {}."sv,
					DMUI_ResultToString(result));
			}
			dmui::ui::Spacing();
		}

		void DrawFaqSection() noexcept
		{
			ReportPresentationResult(Client().DrawSectionHeader(
				"FAQ",
				DearModdingUI::PhosphorGlyph::kQuestion));
			ReportPresentationResult(Client().DrawFaq("Addictol.Faq", kFaqEntries));
		}

		void DrawModdingStateSection() noexcept
		{
			ReportPresentationResult(Client().DrawSectionHeader(
				"On the state of F4SE mods",
				DearModdingUI::PhosphorGlyph::kShieldCheck));
			ReportPresentationResult(dmui::DrawStyledText(
				Client(),
				"CommonLibF4 is GPL-3.0. If a plugin links it and ships without "
				"source, that is a license violation. Not a style disagreement, "
				"not a preference. A violation. You are entitled to the source. "
				"Ask for it.",
				kProseText));
			dmui::ui::Spacing();
			ReportPresentationResult(dmui::DrawStyledText(
				Client(),
				"The rest isn't a legal matter, just bad practice. Code generated "
				"by a model, understood by nobody, shipped as an engine fix with "
				"no measurement behind it. Implementations lifted out of other "
				"people's mods without permission or credit. Another framework "
				"that does what an existing one already does, splitting the user "
				"base and handing two mods one more way to conflict. Releases "
				"shaped for attention and donation points rather than for being "
				"correct.",
				kProseText));
			dmui::ui::Spacing();
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "Don't take our word for any of it. Check.", kProseText));
			dmui::ui::Spacing();

			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "Before you install:", kProseText));
			{
				dmui::FontGuard font{ Client(), DMUI_FONT_ROLE_SUBTEXT };
				if (!font.Pushed())
				{
					ReportPresentationResult(false);
					return;
				}
				dmui::ui::Indent();
				for (const auto& entry : kModChecks)
				{
					char text[512]{};
					std::snprintf(text, sizeof(text), "%s %s", entry.check, entry.detail);
					ReportPresentationResult(Client().DrawBulletText(text));
				}
				dmui::ui::Unindent();
				ReportPresentationResult(font.End());
			}
			dmui::ui::Spacing();

			ReportPresentationResult(dmui::DrawStyledText(
				Client(),
				"New to this? The Midnight Ride is a maintained, opinionated "
				"guide that gets you to a stable Fallout 4 without guesswork. "
				"Start there, then add.",
				kProseText));
			dmui::ui::Spacing();
		}
	}

	void DrawHomePage([[maybe_unused]] void* a_userData) noexcept
	{
		DrawWelcomeSection();
		DrawQuickLinksSection();
		DrawModdingStateSection();
		DrawFaqSection();
	}

	void CopyDiagnosticsSummaryToClipboard(
		[[maybe_unused]] void* a_userData) noexcept
	{
		const auto counts =
			Plugin::GetSingleton()->GetModules().ModuleOutcomeCounts();
		const auto total =
			counts[0] + counts[1] + counts[2] + counts[3] + counts[4];
		static const auto runtime =
			REX::FModule::GetExecutingModule().GetFileVersion();
		char summary[512]{};
		std::snprintf(
			summary,
			sizeof(summary),
			"Game runtime: %u.%u.%u.%u\n"
			"Addictol modules: %llu installed, %llu disabled, %llu skipped, "
			"%llu failed query, %llu failed install (%llu total)",
			runtime.major(),
			runtime.minor(),
			runtime.patch(),
			runtime.build(),
			static_cast<unsigned long long>(counts[0]),
			static_cast<unsigned long long>(counts[1]),
			static_cast<unsigned long long>(counts[2]),
			static_cast<unsigned long long>(counts[3]),
			static_cast<unsigned long long>(counts[4]),
			static_cast<unsigned long long>(total));
		dmui::ui::SetClipboardText(summary);
	}
}
