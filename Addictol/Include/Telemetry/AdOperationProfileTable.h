#pragma once

#include <Telemetry/AdOperationProfile.h>

namespace Addictol
{
	// Fixed compile-time names; views are formed only after the table has stable storage.
	struct OperationProfileName
	{
		std::array<char, 160> text{};
		size_t size{ 0 };

		constexpr void Append(std::string_view a_part)
		{
			for (const auto character : a_part)
				text.at(size++) = character == '-' ? '_' : character;
		}

		[[nodiscard]] constexpr std::string_view View() const noexcept
		{
			return { text.data(), size };
		}
	};

	struct OperationProfileNames
	{
		OperationProfileName series;
		OperationProfileName metric;
		OperationProfileName group;
	};

	template<size_t N>
	[[nodiscard]] constexpr auto MakeOperationProfileDescriptors(
		const std::array<OperationProfileNames, N>& a_names, uint32_t a_period)
	{
		std::array<OperationProfileDescriptor, N> descriptors{};
		for (size_t index = 0; index < N; ++index)
		{
			const auto& names = a_names[index];
			descriptors[index] = {
				names.series.View(), names.series.View(), names.metric.View(),
				a_period, names.group.View()
			};
		}
		return descriptors;
	}

	[[nodiscard]] constexpr OperationProfileNames MakeOperationProfileNames(
		OperationProfileName a_group, std::string_view a_result)
	{
		OperationProfileNames names{ a_group, {}, a_group };
		names.series.Append(".");
		names.series.Append(a_result);
		names.metric.Append("profile.");
		names.metric.Append(names.series.View());
		names.metric.Append(".operations");
		return names;
	}
}
