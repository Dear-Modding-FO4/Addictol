#include <Menu/AdMenuSettings.h>

#include <Core/Settings/AdSettingsModel.h>
#include <DearModdingUI/IconGlyphs.h>
#include <DearModdingUI/Shell.h>
#include <DearModdingUI/Theme.h>

#include <REX/REX.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

namespace Addictol::Menu
{
	using namespace std::literals;

	namespace
	{
		SettingsDraftState g_draft;
		SettingFilter g_filter;
		std::array<bool, kSettingDisplayCategoryOrder.size()> g_expanded{
			true,
			true,
			true,
			true,
			true,
			true,
			true
		};
		bool g_drawnThisFrame{ false };
		std::string g_error;

		[[nodiscard]] size_t CategoryIndex(
			SettingDisplayCategory a_category) noexcept
		{
			const auto position = std::ranges::find(
				kSettingDisplayCategoryOrder,
				a_category);
			return static_cast<size_t>(
				position - kSettingDisplayCategoryOrder.begin());
		}

		[[nodiscard]] char32_t CategoryGlyph(
			SettingDisplayCategory a_category) noexcept
		{
			return DearModdingUI::ResolveIconGlyph(
				DearModdingUI::IconKind::kCategory,
				SettingDisplayCategoryName(a_category));
		}

		void EnsureDraft()
		{
			if (!g_draft.active)
			{
				g_draft = BeginSettingsDraft(
					SettingsRepository::GetSingleton().Snapshot());
			}
		}

		void DiscardDraft() noexcept
		{
			if (g_draft.active)
				LeaveSettingsDraft(g_draft);
			g_error.clear();
		}

		[[nodiscard]] bool DrawControl(SettingDraftEntry& a_entry)
		{
			auto changed = false;
			const auto& setting = *a_entry.setting;
			ImGui::SetNextItemWidth(-FLT_MIN);
			switch (SelectSettingControl(setting))
			{
			case SettingControlKind::kCheckbox:
			{
				auto value = std::get<bool>(a_entry.draft);
				changed = ImGui::Checkbox("##Value", &value);
				if (changed)
					a_entry.draft = value;
				break;
			}
			case SettingControlKind::kCombo:
			{
				auto& value = std::get<std::string>(a_entry.draft);
				if (ImGui::BeginCombo("##Value", value.c_str()))
				{
					for (const auto option : SettingStringOptions(setting))
					{
						const auto selected = option == value;
						if (ImGui::Selectable(
								std::string{ option }.c_str(),
								selected))
						{
							value = option;
							changed = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				break;
			}
			case SettingControlKind::kTextInput:
			{
				auto& value = std::get<std::string>(a_entry.draft);
				std::array<char, 512> buffer{};
				strncpy_s(buffer.data(), buffer.size(), value.c_str(), _TRUNCATE);
				if (ImGui::InputText(
						"##Value",
						buffer.data(),
						buffer.size()))
				{
					value = buffer.data();
					changed = true;
				}
				break;
			}
			case SettingControlKind::kSlider:
			case SettingControlKind::kDrag:
			case SettingControlKind::kNumericInput:
			{
				const auto control = SelectSettingControl(setting);
				const auto range = setting.NumericRange();
				const auto draw = [&](ImGuiDataType a_type,
					void* a_value,
					const char* a_format) {
					if (control == SettingControlKind::kNumericInput)
						return ImGui::InputScalar(
							"##Value",
							a_type,
							a_value);
					if (control == SettingControlKind::kSlider)
					{
						const auto minimum = *range->minimum;
						const auto maximum = *range->maximum;
						if (a_type == ImGuiDataType_Double)
						{
							return ImGui::SliderScalar(
								"##Value",
								a_type,
								a_value,
								&minimum,
								&maximum,
								a_format,
								ImGuiSliderFlags_AlwaysClamp);
						}
						if (a_type == ImGuiDataType_S64)
						{
							const auto minValue =
								static_cast<int64_t>(minimum);
							const auto maxValue =
								static_cast<int64_t>(maximum);
							return ImGui::SliderScalar(
								"##Value",
								a_type,
								a_value,
								&minValue,
								&maxValue,
								a_format,
								ImGuiSliderFlags_AlwaysClamp);
						}
						const auto minValue =
							static_cast<uint64_t>((std::max)(minimum, 0.0));
						const auto maxValue =
							static_cast<uint64_t>((std::max)(maximum, 0.0));
						return ImGui::SliderScalar(
							"##Value",
							a_type,
							a_value,
							&minValue,
							&maxValue,
							a_format,
							ImGuiSliderFlags_AlwaysClamp);
					}
					return ImGui::DragScalar(
						"##Value",
						a_type,
						a_value,
						a_type == ImGuiDataType_Double ? 0.01f : 1.0f,
						nullptr,
						nullptr,
						a_format,
						ImGuiSliderFlags_AlwaysClamp);
				};

				switch (setting.Type())
				{
				case SettingValueType::kFloat32:
					changed = draw(
						ImGuiDataType_Double,
						&std::get<double>(a_entry.draft),
						"%.3f");
					break;
				case SettingValueType::kInt32:
					changed = draw(
						ImGuiDataType_S64,
						&std::get<int64_t>(a_entry.draft),
						"%lld");
					break;
				case SettingValueType::kUInt32:
					changed = draw(
						ImGuiDataType_U64,
						&std::get<uint64_t>(a_entry.draft),
						"%llu");
					break;
				default:
					break;
				}

				if (changed)
				{
					std::visit(
						[&](auto& a_value) {
							using T =
								std::remove_cvref_t<decltype(a_value)>;
							if constexpr (
								std::is_same_v<T, double> ||
								std::is_same_v<T, int64_t> ||
								std::is_same_v<T, uint64_t>)
							{
								auto number =
									static_cast<double>(a_value);
								if constexpr (std::is_same_v<T, double>)
								{
									if (!std::isfinite(number))
									{
										number = std::get<double>(
											setting.DefaultValue());
									}
									number = std::clamp(
										number,
										-static_cast<double>(
											(std::numeric_limits<float>::max)()),
										static_cast<double>(
											(std::numeric_limits<float>::max)()));
								}
								else if constexpr (
									std::is_same_v<T, int64_t>)
								{
									number = std::clamp(
										number,
										static_cast<double>(
											(std::numeric_limits<int32_t>::min)()),
										static_cast<double>(
											(std::numeric_limits<int32_t>::max)()));
								}
								else
								{
									number = (std::min)(
										number,
										static_cast<double>(
											(std::numeric_limits<uint32_t>::max)()));
								}
								if (range && range->minimum)
									number = (std::max)(
										number,
										*range->minimum);
								if (range && range->maximum)
									number = (std::min)(
										number,
										*range->maximum);
								a_value = static_cast<T>(number);
							}
						},
						a_entry.draft);
				}
				break;
			}
			}
			return changed;
		}

		void DrawSetting(SettingDraftEntry& a_entry)
		{
			const auto& setting = *a_entry.setting;
			ImGui::PushID(setting.Section().data());
			ImGui::PushID(setting.Key().data());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			{
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kSubheading
				};
				ImGui::TextUnformatted(
					setting.DisplayName().data(),
					setting.DisplayName().data() +
						setting.DisplayName().size());
			}
			ImGui::SameLine();
			ImGui::TextDisabled(
				"[%.*s]",
				static_cast<int>(setting.Key().size()),
				setting.Key().data());
			if (setting.ApplyTiming() == SettingApplyTiming::kImmediate)
			{
				ImGui::SameLine();
				ImGui::TextColored(
					DearModdingUI::Theme::colors::Accent(),
					"Applies now");
			}
			{
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kSubtext
				};
				ImGui::TextWrapped(
					"%.*s",
					static_cast<int>(setting.Description().size()),
					setting.Description().data());
			}

			ImGui::TableSetColumnIndex(1);
			if (ImGui::BeginTable(
					"##SettingControls",
					2,
					ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn(
					"##ValueColumn",
					ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(
					"##ResetColumn",
					ImGuiTableColumnFlags_WidthFixed,
					ImGui::CalcTextSize("Reset").x +
						ImGui::GetStyle().FramePadding.x * 2.0f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				(void)DrawControl(a_entry);
				ImGui::TableSetColumnIndex(1);
				const auto modified =
					IsSettingModified(setting, a_entry.draft);
				ImGui::BeginDisabled(!modified);
				if (ImGui::Button("Reset"))
					ResetSettingDraft(a_entry);
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(
						ImGuiHoveredFlags_AllowWhenDisabled |
						ImGuiHoveredFlags_DelayNormal))
				{
					ImGui::SetTooltip(
						modified ?
							"Load this setting's shipped default into the draft." :
							"This setting already uses its shipped default.");
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
			ImGui::PopID();
		}

		[[nodiscard]] size_t MatchingCategoryCount(
			SettingDisplayCategory a_category)
		{
			return static_cast<size_t>(std::ranges::count_if(
				g_draft.entries,
				[&](const SettingDraftEntry& a_entry) {
					return a_entry.setting->DisplayCategory() == a_category &&
						IsSettingsPageEditable(*a_entry.setting) &&
						MatchesSettingFilter(
							*a_entry.setting,
							a_entry.draft,
							g_filter);
				}));
		}

		void DrawActions()
		{
			const auto pending = SettingsDraftPendingCount(g_draft);
			char applyLabel[32]{};
			std::snprintf(
				applyLabel,
				sizeof(applyLabel),
				"Apply (%zu)",
				pending);
			const auto applyWidth =
				ImGui::CalcTextSize("Apply (121)").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;
			const auto revertWidth =
				ImGui::CalcTextSize("Revert").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;
			const auto resetWidth =
				ImGui::CalcTextSize("Reset all").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;

			ImGui::BeginDisabled(pending == 0);
			if (ImGui::Button(applyLabel, { applyWidth, 0.0f }))
			{
				const auto commit = PrepareSettingsDraftApply(g_draft);
				const auto result =
					SettingsRepository::GetSingleton().Apply(commit.values);
				if (result.success)
				{
					CompleteSettingsDraftApply(g_draft, commit);
					g_error.clear();
				}
				else
				{
					g_error = result.error;
					REX::WARN(
						"Settings: AddictolCustom.toml could not be saved: {}"sv,
						result.error);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert", { revertWidth, 0.0f }))
			{
				RevertSettingsDraft(g_draft);
				g_error.clear();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Reset all", { resetWidth, 0.0f }))
				ResetSettingsDraftToDefaults(g_draft);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			{
				ImGui::SetTooltip(
					"Load every Addictol page setting's shipped default into the draft. "
					"Use Apply to save them.");
			}
		}

		void DrawFilters()
		{
			std::array<char, 256> search{};
			strncpy_s(
				search.data(),
				search.size(),
				g_filter.search.c_str(),
				_TRUNCATE);
			const auto available = ImGui::GetContentRegionAvail().x;
			const auto modifiedWidth =
				ImGui::GetFrameHeight() +
				ImGui::GetStyle().ItemInnerSpacing.x +
				ImGui::CalcTextSize("Modified only").x;
			const auto searchWidth = (std::max)(
				ImGui::GetFontSize() * 10.0f,
				(std::min)(
					ImGui::GetFontSize() * 24.0f,
					available -
						modifiedWidth -
						ImGui::GetStyle().ItemSpacing.x));
			ImGui::SetNextItemWidth((std::min)(searchWidth, available));
			if (ImGui::InputTextWithHint(
					"##AddictolSettingsSearch",
					"Search settings...",
					search.data(),
					search.size()))
				g_filter.search = search.data();
			if (searchWidth + modifiedWidth +
					ImGui::GetStyle().ItemSpacing.x <=
				available)
				ImGui::SameLine();
			ImGui::Checkbox("Modified only", &g_filter.modifiedOnly);
		}
	}

	void BeginSettingsPageFrame() noexcept
	{
		g_drawnThisFrame = false;
	}

	void EndSettingsPageFrame(bool a_menuVisible) noexcept
	{
		if (!a_menuVisible)
		{
			CloseSettingsPage();
			return;
		}
		if (!g_drawnThisFrame)
			DiscardDraft();
	}

	void CloseSettingsPage() noexcept
	{
		DiscardDraft();
		g_filter = {};
		g_expanded.fill(true);
	}

	void DrawSettingsPage([[maybe_unused]] void* a_userData) noexcept
	{
		g_drawnThisFrame = true;
		EnsureDraft();
		DrawActions();
		DrawFilters();
		ImGui::Spacing();
		ImGui::TextWrapped(
			"Most settings take effect after the next game launch. "
			"Only settings labeled \"Applies now\" update immediately.");
		ImGui::TextDisabled(
			"Menu appearance is owned by the gear panel's Interface Settings.");
		if (!g_error.empty())
		{
			ImGui::TextColored(
				DearModdingUI::Theme::kStatusPaletteDefaults.error,
				"Could not save AddictolCustom.toml: %s",
				g_error.c_str());
		}
		ImGui::Spacing();

		for (const auto category : kSettingDisplayCategoryOrder)
		{
			const auto count = MatchingCategoryCount(category);
			if (count == 0)
				continue;
			const auto index = CategoryIndex(category);
			const auto name = SettingDisplayCategoryName(category);
			{
				const DearModdingUI::Theme::FontGuard font{
					DearModdingUI::Theme::FontRole::kHeading
				};
				DearModdingUI::DrawCollapsingSectionHeader(
					name.data(),
					name.data(),
					CategoryGlyph(category),
					g_expanded[index],
					count);
			}
			if (!g_expanded[index])
				continue;

			if (ImGui::BeginTable(
					name.data(),
					2,
					ImGuiTableFlags_SizingStretchProp |
						ImGuiTableFlags_RowBg |
						ImGuiTableFlags_BordersInnerH |
						ImGuiTableFlags_PadOuterX))
			{
				ImGui::TableSetupColumn(
					"Setting",
					ImGuiTableColumnFlags_WidthStretch,
					3.0f);
				ImGui::TableSetupColumn(
					"Value",
					ImGuiTableColumnFlags_WidthStretch,
					2.0f);
				for (auto& entry : g_draft.entries)
				{
					if (entry.setting->DisplayCategory() != category ||
						!IsSettingsPageEditable(*entry.setting) ||
						!MatchesSettingFilter(
							*entry.setting,
							entry.draft,
							g_filter))
						continue;
					DrawSetting(entry);
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
		}
	}
}
