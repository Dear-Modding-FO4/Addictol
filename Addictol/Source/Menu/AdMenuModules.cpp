#include <Menu/AdMenuModules.h>

#include <Core/AdPlugin.h>
#include <DearModdingUI/IconGlyphs.h>
#include <DearModdingUI/Shell.h>
#include <DearModdingUI/Theme.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Addictol::Menu
{
	namespace
	{
		struct ModulesPageState
		{
			std::string search;
			ModuleOutcomeFilter filter{ ModuleOutcomeFilter::kAll };
		};

		[[nodiscard]] const ImVec4& OutcomeColor(
			ModuleOutcomeSeverity a_severity) noexcept
		{
			using enum ModuleOutcomeSeverity;
			switch (a_severity)
			{
			case kDisabled:
				return DearModdingUI::Theme::kStatusPaletteDefaults.disable;
			case kInfo:
				return DearModdingUI::Theme::kStatusPaletteDefaults.info;
			case kWarning:
				return DearModdingUI::Theme::kStatusPaletteDefaults.warning;
			case kError:
				return DearModdingUI::Theme::kStatusPaletteDefaults.error;
			case kNormal:
				return ImGui::GetStyleColorVec4(ImGuiCol_Text);
			}
			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}

		[[nodiscard]] std::string_view FilterLabel(
			ModuleOutcomeFilter a_filter) noexcept
		{
			for (const auto& option : kModuleOutcomeFilters)
			{
				if (option.filter == a_filter)
					return option.label;
			}
			return kModuleOutcomeFilters.front().label;
		}

		void DrawSummary(const ModuleOutcomeTally& a_counts) noexcept
		{
			const auto total =
				a_counts[0] + a_counts[1] + a_counts[2] + a_counts[3] + a_counts[4];
			ImGui::Text(
				"Modules: %llu installed, %llu disabled, %llu skipped",
				static_cast<unsigned long long>(a_counts[0]),
				static_cast<unsigned long long>(a_counts[1]),
				static_cast<unsigned long long>(a_counts[2]));
			ImGui::TextColored(
				a_counts[3] == 0 && a_counts[4] == 0 ?
					DearModdingUI::Theme::kStatusPaletteDefaults.success :
					DearModdingUI::Theme::kStatusPaletteDefaults.error,
				"%llu failed query, %llu failed install (%llu total)",
				static_cast<unsigned long long>(a_counts[3]),
				static_cast<unsigned long long>(a_counts[4]),
				static_cast<unsigned long long>(total));
		}

		void DrawFilters(ModulesPageState& a_state) noexcept
		{
			if (!ImGui::BeginTable(
					"##module_filters",
					2,
					ImGuiTableFlags_SizingStretchProp))
				return;
			ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch, 3.0f);
			ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableHeadersRow();
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DearModdingUI::DrawSearchInput(
				"ModuleSearchBar",
				"Search modules...",
				a_state.search);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			const auto preview = FilterLabel(a_state.filter);
			if (ImGui::BeginCombo(
					"##module_outcome_filter",
					preview.data()))
			{
				for (const auto& option : kModuleOutcomeFilters)
				{
					const auto selected = option.filter == a_state.filter;
					if (ImGui::Selectable(option.label.data(), selected))
						a_state.filter = option.filter;
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::EndTable();
		}

		void DrawModuleName(const ModuleStatusSnapshot& a_status) noexcept
		{
			ImGui::TextUnformatted(a_status.name.c_str());
			if (!a_status.stage.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", a_status.stage.c_str());
			}
		}

		void DrawModuleDetail(const ModuleStatusSnapshot& a_status) noexcept
		{
			if (a_status.outcome == ModuleOutcome::kSkipped)
			{
				ImGui::TextColored(
					DearModdingUI::Theme::kStatusPaletteDefaults.warning,
					"%s",
					a_status.skipReason.c_str());
			}
			else if (a_status.outcome == ModuleOutcome::kDisabled)
			{
				if (!a_status.settingKey.empty())
				{
					ImGui::Text(
						"Setting: [%s] %s",
						a_status.settingSection.c_str(),
						a_status.settingKey.c_str());
				}
				else
					ImGui::TextUnformatted("Disabled by configuration");
			}
		}

		void DrawModulesTable(
			const std::vector<ModuleStatusSnapshot>& a_statuses,
			const ModulesPageState& a_state) noexcept
		{
			const auto height = (std::max)(ImGui::GetContentRegionAvail().y, 160.0f);
			if (!ImGui::BeginTable(
					"##modules",
					3,
					ImGuiTableFlags_BordersInnerH |
						ImGuiTableFlags_RowBg |
						ImGuiTableFlags_ScrollY |
						ImGuiTableFlags_SizingStretchProp,
					{ 0.0f, height }))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Reason / detail", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableHeadersRow();

			size_t visibleCount{ 0 };
			for (const auto& status : a_statuses)
			{
				if (!MatchesModuleStatus(
						status.name,
						status.outcome,
						a_state.search,
						a_state.filter))
					continue;
				++visibleCount;
				ImGui::PushID(static_cast<int>(visibleCount));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawModuleName(status);
				ImGui::TableSetColumnIndex(1);
				const auto presentation = ClassifyModuleOutcome(status.outcome);
				ImGui::TextColored(
					OutcomeColor(presentation.severity),
					"%.*s",
					static_cast<int>(presentation.label.size()),
					presentation.label.data());
				ImGui::TableSetColumnIndex(2);
				DrawModuleDetail(status);
				ImGui::PopID();
			}

			if (visibleCount == 0)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextDisabled("No modules match the current search and outcome filter.");
			}
			ImGui::EndTable();
		}
	}

	void DrawModulesPage([[maybe_unused]] void* a_userData) noexcept
	{
		static ModulesPageState state;
		const auto statuses =
			Plugin::GetSingleton()->GetModules().ModuleStatuses();
		const auto counts = TallyModuleOutcomes(statuses);

		DearModdingUI::DrawSectionHeader(
			"Modules",
			DearModdingUI::PhosphorGlyph::kPuzzlePiece);
		DrawSummary(counts);
		ImGui::Spacing();
		DrawFilters(state);
		ImGui::Spacing();
		DrawModulesTable(statuses, state);
	}
}
