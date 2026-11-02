#pragma once

#include <Menu/AdMenu.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <vector>

namespace Addictol::Menu
{
	// Sort specs and row colors arrived in ABI 2.2; older hosts draw plain tables.
	[[nodiscard]] inline bool TableFeaturesSupported() noexcept
	{
		return Client().AbiMinor() >= 2;
	}

	[[nodiscard]] inline dmui::ui::TableFlags SortableTableFlags(
		dmui::ui::TableFlags a_flags) noexcept
	{
		if (!TableFeaturesSupported())
			return a_flags;
		return a_flags | dmui::ui::TableFlags::kSortable |
			dmui::ui::TableFlags::kSortTristate;
	}

	inline void TintTableRow(DMUI_Vec4 a_color) noexcept
	{
		if (!TableFeaturesSupported())
			return;
		constexpr float kAlpha{ 0.18f };
		dmui::ui::TableSetBgColor(
			dmui::ui::TableBgTarget::kRowBg1,
			dmui::ui::ColorConvertFloat4ToU32(
				{ a_color.x, a_color.y, a_color.z, a_color.w * kAlpha }));
	}

	// Call after TableHeadersRow; a_compare(column, lhs, rhs) -> weak_ordering.
	template <class Row, class Compare>
	void SortTableRows(
		std::vector<const Row*>& a_rows,
		Compare&& a_compare)
	{
		if (!TableFeaturesSupported())
			return;
		const auto specs = dmui::ui::TableGetSortSpecs();
		if (specs.empty())
			return;
		std::ranges::stable_sort(a_rows, [&](const Row* a_lhs, const Row* a_rhs) {
			for (const auto& spec : specs)
			{
				const std::weak_ordering order = a_compare(spec.column, *a_lhs, *a_rhs);
				if (order != 0)
				{
					return spec.direction == DMUI_UI_SORT_DIRECTION_DESCENDING ?
						order > 0 :
						order < 0;
				}
			}
			return false;
		});
	}
}
