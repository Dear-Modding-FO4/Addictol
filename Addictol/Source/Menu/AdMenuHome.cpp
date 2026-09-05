#include <Menu/AdMenuHome.h>

#include <Core/AdPlugin.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuWidgets.h>

#include <DearModdingUI/ImGuiForward.h>

#include <array>
#include <cstdio>
#include <cstdint>

namespace Addictol::Menu
{
	namespace
	{
		constexpr std::array kQuickLinks{
			dmui::Link{
				"Nexus Mods",
				"https://www.nexusmods.com/fallout4/mods/84214" },
			dmui::Link{
				"GitHub",
				"https://github.com/Dear-Modding-FO4/Addictol" }
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
			{
				const MenuUi::ScopedFont font{ DMUI_FONT_ROLE_TITLE };
				ImGui::TextUnformatted("Welcome to Addictol");
			}
			ImGui::Spacing();
			{
				const MenuUi::ScopedFont font{ DMUI_FONT_ROLE_SUBTEXT };
				ImGui::TextWrapped(
					"Addictol combines engine fixes, crash fixes, and performance patches "
					"for Fallout 4 in a single F4SE plugin. Use the pages on the left to "
					"inspect live diagnostics and runtime behavior.");
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const auto counts =
				Plugin::GetSingleton()->GetModules().ModuleOutcomeCounts();
			const auto total =
				counts[0] + counts[1] + counts[2] + counts[3] + counts[4];
			static const auto runtime =
				REX::FModule::GetExecutingModule().GetFileVersion();
			ImGui::Text(
				"Game runtime: %u.%u.%u.%u",
				runtime.major(),
				runtime.minor(),
				runtime.patch(),
				runtime.build());
			ImGui::Text(
				"Modules: %llu installed, %llu disabled, %llu skipped",
				static_cast<unsigned long long>(counts[0]),
				static_cast<unsigned long long>(counts[1]),
				static_cast<unsigned long long>(counts[2]));
			ImGui::TextColored(
				counts[3] == 0 && counts[4] == 0 ?
					dmui::ToImVec4(ThemeColors().statusSuccess) :
					dmui::ToImVec4(ThemeColors().statusError),
				"%llu failed query, %llu failed install (%llu total)",
				static_cast<unsigned long long>(counts[3]),
				static_cast<unsigned long long>(counts[4]),
				static_cast<unsigned long long>(total));
			ImGui::Spacing();
		}

		void DrawQuickLinksSection() noexcept
		{
			(void)Client().DrawSectionHeader("Quick Links");
			(void)Client().DrawLinkRow("Addictol.QuickLinks", kQuickLinks);
			ImGui::Spacing();
		}

		void DrawFaqSection() noexcept
		{
			(void)Client().DrawSectionHeader("FAQ");
			(void)Client().DrawFaq("Addictol.Faq", kFaqEntries);
		}

		void DrawModdingStateSection() noexcept
		{
			(void)Client().DrawSectionHeader("On the state of F4SE mods");
			const MenuUi::ScopedFont font{ DMUI_FONT_ROLE_SUBTEXT };
			ImGui::TextWrapped(
				"CommonLibF4 is GPL-3.0. If a plugin links it and ships without "
				"source, that is a license violation. Not a style disagreement, "
				"not a preference. A violation. You are entitled to the source. "
				"Ask for it.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The rest isn't a legal matter, just bad practice. Code generated "
				"by a model, understood by nobody, shipped as an engine fix with "
				"no measurement behind it. Implementations lifted out of other "
				"people's mods without permission or credit. Another framework "
				"that does what an existing one already does, splitting the user "
				"base and handing two mods one more way to conflict. Releases "
				"shaped for attention and donation points rather than for being "
				"correct.");
			ImGui::Spacing();
			ImGui::TextWrapped("Don't take our word for any of it. Check.");
			ImGui::Spacing();

			ImGui::TextWrapped("Before you install:");
			ImGui::Indent();
			for (const auto& entry : kModChecks)
			{
				char text[512]{};
				std::snprintf(text, sizeof(text), "%s %s", entry.check, entry.detail);
				(void)Client().DrawBulletText(text);
			}
			ImGui::Unindent();
			ImGui::Spacing();

			ImGui::TextWrapped(
				"New to this? The Midnight Ride is a maintained, opinionated "
				"guide that gets you to a stable Fallout 4 without guesswork. "
				"Start there, then add.");
			ImGui::Spacing();
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
		ImGui::SetClipboardText(summary);
	}
}
