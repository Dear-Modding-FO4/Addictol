#include <DearModdingUI/HostSettingsView.h>

#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/Shell.h>
#include <DearModdingUI/Theme.h>
#include <Menu/AdMenu.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace Addictol::DearModdingUI
{
	namespace
	{
		struct AccentPreset
		{
			const char* name;
			const char* description;
			HostAccentColor color;
		};

		inline constexpr std::array kAccentPresets{
			AccentPreset{
				"Default green",
				"Default green",
				{ 0x42, 0xFA, 0x60 }
			},
			AccentPreset{
				"Accessible blue",
				"Okabe-Ito blue, distinguishable across common color-vision deficiencies",
				{ 0x00, 0x72, 0xB2 }
			},
			AccentPreset{
				"Accessible orange",
				"Okabe-Ito orange, distinguishable across common color-vision deficiencies",
				{ 0xE6, 0x9F, 0x00 }
			},
			AccentPreset{
				"Accessible sky blue",
				"Okabe-Ito sky blue, distinguishable across common color-vision deficiencies",
				{ 0x56, 0xB4, 0xE9 }
			},
			AccentPreset{
				"Accessible vermillion",
				"Okabe-Ito vermillion, distinguishable across common color-vision deficiencies",
				{ 0xD5, 0x5E, 0x00 }
			},
			AccentPreset{
				"Accessible purple",
				"Okabe-Ito purple, distinguishable across common color-vision deficiencies",
				{ 0xCC, 0x79, 0xA7 }
			}
		};
		HostSettingsDraftState g_settingsDraft;
		uint64_t g_observedPanelRevision{ 0 };

		[[nodiscard]] float ControlWidth() noexcept
		{
			return (std::min)(
				ImGui::GetContentRegionAvail().x,
				ImGui::GetFontSize() * 22.0f);
		}

		void DrawHelp(const char* a_text) noexcept
		{
			const Theme::FontGuard font{ Theme::FontRole::kSubtext };
			ImGui::TextDisabled("%s", a_text);
			ImGui::Spacing();
		}

		void PreviewDraft() noexcept
		{
			HostSettings::SetPreview(
				PreviewHostInterfaceSettings(g_settingsDraft.draft),
				g_observedPanelRevision);
			Theme::ApplyStyle();
		}

		void EnsureDraft() noexcept
		{
			const auto revision = HostSettings::PanelRevision();
			if (g_settingsDraft.active &&
				g_observedPanelRevision == revision)
				return;

			if (g_settingsDraft.active)
				LeaveHostSettingsDraft(g_settingsDraft);
			g_settingsDraft = BeginHostSettingsDraft(
				HostSettings::Current());
			g_observedPanelRevision = revision;
			PreviewDraft();
		}

		void ApplyDraft() noexcept
		{
			auto commit = ApplyHostSettingsDraft(g_settingsDraft);
			if (!commit.settings)
				return;

			HostSettings::Apply(*commit.settings);
			g_settingsDraft = BeginHostSettingsDraft(
				HostSettings::Current());
			PreviewDraft();
		}

		void DrawAccentPresets(
			HostInterfaceSettings& a_settings,
			bool& a_changed) noexcept
		{
			ImGui::TextUnformatted("Color-vision-friendly presets");
			const auto swatchSize = ImGui::GetFrameHeight();
			for (size_t index = 0; index < kAccentPresets.size(); ++index)
			{
				const auto& preset = kAccentPresets[index];
				ImGui::PushID(static_cast<int>(index));
				if (index > 0)
					ImGui::SameLine();
				if (ImGui::ColorButton(
						preset.name,
						HostAccentToImVec4(preset.color),
						ImGuiColorEditFlags_NoAlpha,
						{ swatchSize, swatchSize }))
				{
					a_settings.accentColor = preset.color;
					a_changed = true;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", preset.description);
				ImGui::PopID();
			}
			ImGui::Spacing();
		}

		void DrawAppearance() noexcept
		{
			DrawSectionHeader("Appearance");
			auto& settings = g_settingsDraft.draft;
			auto changed = false;

			ImGui::TextUnformatted("Accent color");
			DrawHelp(
				"Retints selections, controls, links, and every Phosphor menu icon in colored mode.");
			auto accent = HostAccentToImVec4(settings.accentColor);
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::ColorPicker3(
					"##DearModdingUI.AccentColor",
					&accent.x,
					ImGuiColorEditFlags_NoAlpha |
						ImGuiColorEditFlags_DisplayRGB |
						ImGuiColorEditFlags_InputRGB |
						ImGuiColorEditFlags_PickerHueBar))
			{
				settings.accentColor = HostAccentFromImVec4(accent);
				changed = true;
			}
			DrawAccentPresets(settings, changed);

			auto iconMode =
				settings.iconColorMode == Theme::IconColorMode::kMonochrome ? 1 : 0;
			constexpr const char* iconModes[]{ "Colored (accent)", "Monochrome (text)" };
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::Combo(
					"Icon color mode",
					&iconMode,
					iconModes,
					static_cast<int>(std::size(iconModes))))
			{
				settings.iconColorMode = iconMode == 1 ?
					Theme::IconColorMode::kMonochrome :
					Theme::IconColorMode::kColored;
				changed = true;
			}
			DrawHelp(
				"Colored icons use the accent above; monochrome icons use the active text color.");

			auto opacityPercent = settings.windowBackgroundOpacity * 100.0f;
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::SliderFloat(
					"Window background opacity",
					&opacityPercent,
					kMinWindowBackgroundOpacity * 100.0f,
					kMaxWindowBackgroundOpacity * 100.0f,
					"%.0f%%",
					ImGuiSliderFlags_AlwaysClamp))
			{
				settings.windowBackgroundOpacity = opacityPercent / 100.0f;
				changed = true;
			}
			DrawHelp(
				"Raises or lowers the darkness of the host window without changing client content.");

			changed |= ImGui::Checkbox(
				"Background blur",
				&settings.backgroundBlur);
			DrawHelp(
				"Blurs the game only behind the host window; disabling it avoids the blur passes.");

			ImGui::BeginDisabled(!settings.backgroundBlur);
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::SliderFloat(
					"Blur strength",
					&settings.backgroundBlurStrength,
					kMinBackgroundBlurStrength,
					kMaxBackgroundBlurStrength,
					"%.2f",
					ImGuiSliderFlags_AlwaysClamp))
				changed = true;
			ImGui::EndDisabled();
			DrawHelp(
				"Adjusts the per-frame blur sample spread without reallocating graphics resources.");

			if (changed)
				PreviewDraft();
		}

		void DrawReadability() noexcept
		{
			DrawSectionHeader("Readability");
			auto& settings = g_settingsDraft.draft;

			ImGui::SetNextItemWidth(ControlWidth());
			ImGui::SliderFloat(
				"UI scale (requires Apply)",
				&settings.uiScale,
				Theme::kMinUserScale,
				Theme::kMaxUserScale,
				"%.2fx",
				ImGuiSliderFlags_AlwaysClamp);
			DrawHelp(
				"Multiplies resolution-derived sizing; Apply rebuilds typography once before the next frame.");
			const auto& families = Theme::AvailableBodyFontFamilies();
			const auto resolvedFamily =
				Theme::ResolveBodyFontFamily(settings.bodyFontFamily);
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::BeginCombo(
					"Body font family (requires Apply)",
					resolvedFamily.data()))
			{
				for (const auto& family : families)
				{
					const auto selected = family == resolvedFamily;
					if (ImGui::Selectable(family.c_str(), selected))
						settings.bodyFontFamily = family;
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			DrawHelp(
				"Lists font-family folders in Data/F4SE/Plugins/DearModdingUI/Fonts; Apply rebuilds the selected family once.");
			const auto effectiveFamily = Theme::EffectiveBodyFontFamily();
			ImGui::TextDisabled(
				"Applied this frame: %.*s",
				static_cast<int>(effectiveFamily.size()),
				effectiveFamily.data());
			ImGui::Spacing();
		}

		void DrawReadOnlyHostFact(
			const char* a_label,
			const char* a_value,
			const char* a_source) noexcept
		{
			{
				const Theme::FontGuard font{ Theme::FontRole::kHeading };
				ImGui::TextUnformatted(a_label);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(a_value);
			DrawHelp(a_source);
		}

		void DrawReadOnlyFacts() noexcept
		{
			DrawSectionHeader("Host facts (read-only)");
			ImGui::TextDisabled(
				"These values are informational here. Their configuration sources are listed below.");
			ImGui::Spacing();

			char toggleKey[32]{};
			const auto key = Menu::ToggleKeyName();
			std::snprintf(
				toggleKey,
				sizeof(toggleKey),
				"%.*s",
				static_cast<int>(key.size()),
				key.data());
			DrawReadOnlyHostFact(
				"Menu toggle key",
				toggleKey,
				"Configure [Additional] sMenuToggleKey in Data/F4SE/Plugins/AddictolCustom.toml.");

			char refresh[32]{};
			std::snprintf(
				refresh,
				sizeof(refresh),
				"%u ms",
				Menu::RefreshMs());
			DrawReadOnlyHostFact(
				"Menu refresh interval",
				refresh,
				"Configure [Additional] uMenuRefreshMs in Data/F4SE/Plugins/AddictolCustom.toml.");

			const auto* body = Theme::GetFonts().body;
			char typography[32]{};
			std::snprintf(
				typography,
				sizeof(typography),
				"%.0f px",
				body ? body->LegacySize : ImGui::GetFontSize());
			DrawReadOnlyHostFact(
				"Resolved typography size",
				typography,
				"Derived from the backbuffer height and the applied UI scale at a frame boundary.");

			char scale[32]{};
			std::snprintf(
				scale,
				sizeof(scale),
				"%.2fx",
				Theme::Scale());
			DrawReadOnlyHostFact(
				"Effective UI scale",
				scale,
				"Derived from resolution and [Additional] fMenuUiScale.");
		}
	}

	HostSettingsTitleActionAvailability
		GetHostSettingsTitleActionAvailability() noexcept
	{
		EnsureDraft();
		return ResolveHostSettingsTitleActionAvailability(
			HostSettingsDraftDiffers(g_settingsDraft));
	}

	void InvokeHostSettingsTitleAction(
		HostSettingsTitleAction a_action) noexcept
	{
		EnsureDraft();
		switch (a_action)
		{
		case HostSettingsTitleAction::kApply:
			ApplyDraft();
			break;
		case HostSettingsTitleAction::kRevert:
			RevertHostSettingsDraft(g_settingsDraft);
			PreviewDraft();
			break;
		case HostSettingsTitleAction::kReset:
			ResetHostSettingsDraft(g_settingsDraft);
			PreviewDraft();
			break;
		}
	}

	void DrawHostSettingsControls() noexcept
	{
		EnsureDraft();
		DrawAppearance();
		ImGui::Spacing();
		DrawReadability();
		ImGui::Spacing();
		DrawReadOnlyFacts();
	}
}
