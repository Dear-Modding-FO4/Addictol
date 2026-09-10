#include <Menu/AdMenuModules.h>

#include <Core/AdPlugin.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>

#include <DearModdingUI/UI.h>

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

		[[nodiscard]] dmui::ui::Vec4 OutcomeColor(
			ModuleOutcomeSeverity a_severity) noexcept
		{
			using enum ModuleOutcomeSeverity;
			switch (a_severity)
			{
			case kDisabled:
				return dmui::ToUIVec4(ThemeColors().statusDisable);
			case kInfo:
				return dmui::ToUIVec4(ThemeColors().statusInfo);
			case kWarning:
				return dmui::ToUIVec4(ThemeColors().statusWarning);
			case kError:
				return dmui::ToUIVec4(ThemeColors().statusError);
			case kNormal:
				return dmui::ui::GetStyleColor(dmui::ui::Color::kText);
			}
			return dmui::ui::GetStyleColor(dmui::ui::Color::kText);
		}

		void DrawSummary(const ModuleOutcomeTally& a_counts) noexcept
		{
			const auto total =
				a_counts[0] + a_counts[1] + a_counts[2] + a_counts[3] + a_counts[4];
			dmui::ui::Text(
				"Modules: %llu installed, %llu disabled, %llu skipped",
				static_cast<unsigned long long>(a_counts[0]),
				static_cast<unsigned long long>(a_counts[1]),
				static_cast<unsigned long long>(a_counts[2]));
			dmui::ui::TextColored(
				a_counts[3] == 0 && a_counts[4] == 0 ?
					dmui::ToUIVec4(ThemeColors().statusSuccess) :
					dmui::ToUIVec4(ThemeColors().statusError),
				"%llu failed query, %llu failed install (%llu total)",
				static_cast<unsigned long long>(a_counts[3]),
				static_cast<unsigned long long>(a_counts[4]),
				static_cast<unsigned long long>(total));
		}

		void DrawFilters(ModulesPageState& a_state) noexcept
		{
			if (!dmui::ui::BeginTable(
					"##module_filters",
					2,
					dmui::ui::TableFlags::kSizingStretchProp))
				return;
			dmui::ui::TableSetupColumn("Search", dmui::ui::TableColumnFlags::kWidthStretch, 3.0f);
			dmui::ui::TableSetupColumn("Outcome", dmui::ui::TableColumnFlags::kWidthStretch, 1.0f);
			dmui::ui::TableHeadersRow();
			dmui::ui::TableNextRow();
			(void)dmui::ui::TableSetColumnIndex(0);
			const auto search = Client().DrawSearchInput(
				"ModuleSearchBar",
				"Search modules...",
				a_state.search);
			ReportPresentationResult(search.has_value());
			(void)dmui::ui::TableSetColumnIndex(1);
			dmui::ui::SetNextItemWidth(-1.0f);
			static const auto choices = [] {
				std::array<
					dmui::ChoiceOption<ModuleOutcomeFilter>,
					kModuleOutcomeFilters.size()> result;
				for (size_t index = 0; index < kModuleOutcomeFilters.size(); ++index)
				{
					const auto& option = kModuleOutcomeFilters[index];
					result[index] = {
						option.filter,
						std::string{ option.label },
						std::string{ option.key }
					};
				}
				return result;
			}();
			const auto selected = dmui::DrawChoice<ModuleOutcomeFilter>(
				"##module_outcome_filter",
				a_state.filter,
				choices,
				"All outcomes");
			if (selected.changed && selected.selected)
			{
				a_state.filter = *selected.selected;
			}
			dmui::ui::EndTable();
		}

		void DrawModuleName(const ModuleStatusSnapshot& a_status) noexcept
		{
			dmui::ui::TextUnformatted(a_status.name.c_str());
			if (!a_status.stage.empty())
			{
				dmui::ui::SameLine();
				dmui::ui::TextDisabled("(%s)", a_status.stage.c_str());
			}
		}

		void DrawModuleDetail(const ModuleStatusSnapshot& a_status) noexcept
		{
			if (a_status.outcome == ModuleOutcome::kSkipped)
			{
				dmui::ui::TextColored(
					dmui::ToUIVec4(ThemeColors().statusWarning),
					"%s",
					a_status.skipReason.c_str());
			}
			else if (a_status.outcome == ModuleOutcome::kDisabled)
			{
				if (!a_status.settingKey.empty())
				{
					dmui::ui::Text(
						"Setting: [%s] %s",
						a_status.settingSection.c_str(),
						a_status.settingKey.c_str());
				}
				else
					dmui::ui::TextUnformatted("Disabled by configuration");
			}
		}

		void DrawModulesTable(
			const std::vector<ModuleStatusSnapshot>& a_statuses,
			const ModulesPageState& a_state) noexcept
		{
			const auto height = (std::max)(dmui::ui::GetContentRegionAvail().y, 160.0f);
			if (!dmui::ui::BeginTable(
					"##modules",
					3,
					dmui::ui::TableFlags::kBordersInnerH |
						dmui::ui::TableFlags::kRowBg |
						dmui::ui::TableFlags::kScrollY |
						dmui::ui::TableFlags::kSizingStretchProp,
					{ 0.0f, height }))
				return;

			dmui::ui::TableSetupScrollFreeze(0, 1);
			dmui::ui::TableSetupColumn("Module", dmui::ui::TableColumnFlags::kWidthStretch, 2.0f);
			dmui::ui::TableSetupColumn("Outcome", dmui::ui::TableColumnFlags::kWidthStretch, 1.0f);
			dmui::ui::TableSetupColumn("Reason / detail", dmui::ui::TableColumnFlags::kWidthStretch, 2.0f);
			dmui::ui::TableHeadersRow();

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
				dmui::ui::PushID(static_cast<int>(visibleCount));
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableSetColumnIndex(0);
				DrawModuleName(status);
				(void)dmui::ui::TableSetColumnIndex(1);
				const auto presentation = ClassifyModuleOutcome(status.outcome);
				dmui::ui::TextColored(
					OutcomeColor(presentation.severity),
					"%.*s",
					static_cast<int>(presentation.label.size()),
					presentation.label.data());
				(void)dmui::ui::TableSetColumnIndex(2);
				DrawModuleDetail(status);
				dmui::ui::PopID();
			}

			if (visibleCount == 0)
			{
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableSetColumnIndex(0);
				dmui::ui::TextDisabled("No modules match the current search and outcome filter.");
			}
			dmui::ui::EndTable();
		}
	}

	void DrawModulesPage([[maybe_unused]] void* a_userData) noexcept
	{
		static ModulesPageState state;
		const auto statuses =
			Plugin::GetSingleton()->GetModules().ModuleStatuses();
		const auto counts = TallyModuleOutcomes(statuses);

		ReportPresentationResult(Client().DrawSectionHeader(
			"Modules",
			DearModdingUI::PhosphorGlyph::kPuzzlePiece));
		DrawSummary(counts);
		dmui::ui::Spacing();
		DrawFilters(state);
		dmui::ui::Spacing();
		DrawModulesTable(statuses, state);
	}
}
