#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

#include <algorithm>
#include <compare>
#include <cstring>

namespace Addictol
{
	namespace
	{
		using namespace ProfilerMenuUi;

		[[nodiscard]] std::string_view ModuleStatus(const ModuleProfileEntry& a_entry) noexcept
		{
			if (a_entry.skipped)
				return "skipped"sv;
			if (!a_entry.querySuccess)
				return "query failed"sv;
			if (!a_entry.installSuccess)
				return "install failed"sv;
			return "installed"sv;
		}

		[[nodiscard]] bool MatchesFilter(std::string_view a_name, std::string_view a_filter) noexcept
		{
			if (a_filter.empty())
				return true;
			if (a_filter.size() > a_name.size())
				return false;

			for (size_t start = 0; start + a_filter.size() <= a_name.size(); ++start)
			{
				if (EqualsIgnoringCase(a_name.substr(start, a_filter.size()), a_filter))
					return true;
			}
			return false;
		}

		void ApplyOrder(ProfilerMenuModulesCache& a_cache) noexcept
		{
			const std::string_view filter{ a_cache.filter.data() };
			a_cache.order.clear();
			for (size_t index = 0; index < a_cache.entries.size(); ++index)
			{
				if (MatchesFilter(a_cache.entries[index].moduleName, filter))
					a_cache.order.push_back(index);
			}

			const auto& entries = a_cache.entries;
			const auto column = a_cache.sortColumn;
			const auto compare = [&entries, column](size_t a_left, size_t a_right) noexcept -> std::partial_ordering {
				const auto& left = entries[a_left];
				const auto& right = entries[a_right];
				switch (column)
				{
				case 1:
					return left.queryMs <=> right.queryMs;
				case 2:
					return left.installMs <=> right.installMs;
				case 3:
					return ModuleStatus(left) <=> ModuleStatus(right);
				default:
					return left.moduleName <=> right.moduleName;
				}
			};

			const auto ascending = a_cache.sortAscending;
			std::stable_sort(
				a_cache.order.begin(),
				a_cache.order.end(),
				[&compare, ascending](size_t a_left, size_t a_right) noexcept {
					const auto ordering = compare(a_left, a_right);
					return ascending ? ordering < 0 : ordering > 0;
				});
		}
	}

	void DrawProfilerMenuModules(
		ProfilerMenuModel& a_model,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		auto& cache = a_model.MutableModules();
		if (cache.entries.empty())
		{
			Muted("No module timings were recorded. Requires bModuleProfiler."sv);
			PanelFooter(cache.state, a_context);
			return;
		}

		ImGui::SetNextItemWidth(320.0f);
		ImGui::InputText("Filter", cache.filter.data(), cache.filter.size());
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			cache.filter = {};

		if (!ImGui::BeginTable("modules", 4, kSortableTableFlags, ImVec2(0.0f, 480.0f)))
		{
			PanelFooter(cache.state, a_context);
			return;
		}

		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
		ImGui::TableSetupColumn("Query", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Install", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount)
		{
			cache.sortColumn = specs->Specs[0].ColumnIndex;
			cache.sortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
			specs->SpecsDirty = false;
		}
		ApplyOrder(cache);

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(cache.order.size()));
		while (clipper.Step())
		{
			for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const auto& entry = cache.entries[cache.order[static_cast<size_t>(row)]];
				const auto status = ModuleStatus(entry);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(
					entry.moduleName.data(),
					entry.moduleName.data() + entry.moduleName.size());
				ImGui::TableNextColumn();
				MonoCell(FormatMs(entry.queryMs));
				ImGui::TableNextColumn();
				MonoCell(FormatMs(entry.installMs));
				ImGui::TableNextColumn();
				if (entry.skipped)
					Muted(status);
				else if (!entry.querySuccess || !entry.installSuccess)
					Error(status);
				else
					MonoCell(status);
			}
		}
		ImGui::EndTable();

		LabeledValue("Shown"sv, FormatRatio(cache.order.size(), cache.entries.size()));
		PanelFooter(cache.state, a_context);
	}
}
