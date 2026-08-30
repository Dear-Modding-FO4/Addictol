#include <DearModdingUI/HostSettingsView.h>

#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/Shell.h>
#include <DearModdingUI/Theme.h>
#include <Menu/AdMenu.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

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

		void ApplySettings(HostInterfaceSettings a_settings) noexcept
		{
			HostSettings::Apply(std::move(a_settings));
			Theme::ApplyStyle();
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
			auto settings = HostSettings::Current();
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
				ApplySettings(std::move(settings));
		}

		void DrawReadability() noexcept
		{
			DrawSectionHeader("Readability");
			auto settings = HostSettings::Current();
			auto changed = false;

			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::SliderFloat(
					"UI scale",
					&settings.uiScale,
					Theme::kMinUserScale,
					Theme::kMaxUserScale,
					"%.2fx",
					ImGuiSliderFlags_AlwaysClamp))
				changed = true;
			DrawHelp(
				"Multiplies resolution-derived sizing; fonts rebuild safely before the next frame.");

			const auto& families = Theme::AvailableBodyFontFamilies();
			const auto resolvedFamily =
				Theme::ResolveBodyFontFamily(settings.bodyFontFamily);
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::BeginCombo(
					"Body font family",
					resolvedFamily.data()))
			{
				for (const auto& family : families)
				{
					const auto selected = family == resolvedFamily;
					if (ImGui::Selectable(family.c_str(), selected))
					{
						settings.bodyFontFamily = family;
						changed = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			DrawHelp(
				"Lists font-family folders in Data/F4SE/Plugins/DearModdingUI/Fonts; a missing family falls back to Jost.");
			const auto effectiveFamily = Theme::EffectiveBodyFontFamily();
			ImGui::TextDisabled(
				"Effective this frame: %.*s",
				static_cast<int>(effectiveFamily.size()),
				effectiveFamily.data());
			ImGui::Spacing();

			if (changed)
				ApplySettings(std::move(settings));
		}

		void DrawReset() noexcept
		{
			DrawSectionHeader("Behavior and reset");
			ImGui::TextWrapped(
				"Interface changes apply immediately and persist in "
				"Data/F4SE/Plugins/AddictolCustom.toml.");
			ImGui::Spacing();
			if (ImGui::Button("Reset all interface settings"))
			{
				HostSettings::Reset();
				Theme::ApplyStyle();
			}
			DrawHelp(
				"Restores the shipped accent, icon mode, opacity, blur, scale, and Jost body font.");
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
				"Derived from the backbuffer height and the editable UI scale at a frame boundary.");

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

	void DrawHostSettingsControls() noexcept
	{
		DrawAppearance();
		ImGui::Spacing();
		DrawReadability();
		ImGui::Spacing();
		DrawReset();
		ImGui::Spacing();
		DrawReadOnlyFacts();
	}
}
