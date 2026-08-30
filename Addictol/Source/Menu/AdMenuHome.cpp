#include <Menu/AdMenuHome.h>

#include <Core/AdPlugin.h>
#include <DearModdingUI/Shell.h>
#include <DearModdingUI/Theme.h>
#include <Menu/AdMenu.h>

#include <imgui/imgui.h>

#include <array>
#include <cstdio>
#include <cstdint>

namespace Addictol::Menu
{
	namespace
	{
		struct QuickLink
		{
			const char* label;
			const char* url;
			const char* tooltip;
			bool enabled;
		};

		constexpr std::array<QuickLink, 2> kQuickLinks{
			QuickLink{
				"Copy Nexus URL",
				"https://www.nexusmods.com/fallout4/mods/84214",
				"Copy the Addictol Nexus Mods URL to the clipboard.",
				true },
			QuickLink{
				"Copy GitHub URL",
				"https://github.com/Dear-Modding-FO4/Addictol",
				"Copy the Addictol GitHub URL to the clipboard.",
				true }
		};

		struct FaqEntry
		{
			const char* question;
			const char* answer;
			bool showToggleKey;
		};

		constexpr std::array<FaqEntry, 3> kFaqEntries{
			FaqEntry{
				"Which Fallout 4 runtimes are supported?",
				"One Addictol DLL supports OG 1.10.163, NG 1.10.984, and AE 1.11.240.",
				false },
			FaqEntry{
				"Where is the configuration stored?",
				"Defaults are documented in Data/F4SE/Plugins/Addictol.toml. Put overrides in AddictolCustom.toml beside it so updates do not overwrite them.",
				false },
			FaqEntry{
				"How do I open the menu?",
				nullptr,
				true }
		};

		void DrawWelcomeSection() noexcept
		{
			{
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kTitle
				};
				ImGui::TextUnformatted("Welcome to Addictol");
			}
			ImGui::Spacing();
			{
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kSubtext
				};
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
					DearModdingUI::Theme::kStatusPaletteDefaults.success :
					DearModdingUI::Theme::kStatusPaletteDefaults.error,
				"%llu failed query, %llu failed install (%llu total)",
				static_cast<unsigned long long>(counts[3]),
				static_cast<unsigned long long>(counts[4]),
				static_cast<unsigned long long>(total));
			ImGui::Spacing();
		}

		void DrawQuickLinksSection() noexcept
		{
			DearModdingUI::DrawSectionHeader("Quick Links");
			const auto spacing = ImGui::GetStyle().ItemSpacing.x;
			const auto buttonWidth =
				(ImGui::GetContentRegionAvail().x -
					spacing * static_cast<float>(kQuickLinks.size() - 1)) /
				static_cast<float>(kQuickLinks.size());

			for (size_t index = 0; index < kQuickLinks.size(); ++index)
			{
				const auto& link = kQuickLinks[index];
				ImGui::BeginDisabled(!link.enabled);
				const auto clicked =
					ImGui::Button(link.label, { buttonWidth, 0.0f });
				ImGui::EndDisabled();
				if (link.enabled && clicked)
					ImGui::SetClipboardText(link.url);

				const auto hoverFlags =
					ImGuiHoveredFlags_DelayNormal |
					(link.enabled ?
							ImGuiHoveredFlags_None :
							ImGuiHoveredFlags_AllowWhenDisabled);
				if (ImGui::IsItemHovered(hoverFlags))
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(link.tooltip);
					ImGui::EndTooltip();
				}
				if (index + 1 < kQuickLinks.size())
					ImGui::SameLine();
			}
			ImGui::Spacing();
		}

		void DrawFaqSection() noexcept
		{
			DearModdingUI::DrawSectionHeader("FAQ");
			for (const auto& entry : kFaqEntries)
			{
				if (!ImGui::CollapsingHeader(entry.question))
					continue;
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kSubtext
				};
				ImGui::Indent();
				if (entry.showToggleKey)
				{
					const auto key = ToggleKeyName();
					ImGui::TextWrapped(
						"Enable [Additional] bMenu, then press %.*s in game. "
						"The default key is F11 and sMenuToggleKey changes it.",
						static_cast<int>(key.size()),
						key.data());
				}
				else
				{
					ImGui::TextWrapped("%s", entry.answer);
				}
				ImGui::Unindent();
				ImGui::Spacing();
			}
		}
	}

	void DrawHomePage([[maybe_unused]] void* a_userData) noexcept
	{
		DrawWelcomeSection();
		DrawQuickLinksSection();
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
