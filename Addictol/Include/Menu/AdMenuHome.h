#pragma once

#include <DearModdingUI/Client.h>
#include <DearModdingUI/IconGlyphs.h>

#include <array>

namespace Addictol::Menu
{
	inline const std::array kHomeQuickLinks{
		dmui::Link{
			.label = "Nexus Mods",
			.external = {
				.targetKind = DMUI_EXTERNAL_TARGET_URI,
				.target = "https://www.nexusmods.com/fallout4/mods/84214"
			},
			.note = "Open Addictol on Nexus Mods in your browser.",
			.glyph = DearModdingUI::FindPhosphorSlugGlyphOrZero("arrow-square-out"),
			.action = dmui::LinkAction::kOpenExternal
		},
		dmui::Link{
			.label = "GitHub",
			.external = {
				.targetKind = DMUI_EXTERNAL_TARGET_URI,
				.target = "https://github.com/Dear-Modding-FO4/Addictol"
			},
			.note = "Open Addictol on GitHub in your browser.",
			.glyph = DearModdingUI::FindPhosphorSlugGlyphOrZero("github-logo"),
			.action = dmui::LinkAction::kOpenExternal
		}
	};

	void DrawHomePage(void* a_userData) noexcept;
	void CopyDiagnosticsSummaryToClipboard(void* a_userData) noexcept;
}
