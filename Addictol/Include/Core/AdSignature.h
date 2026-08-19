#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace RELEX
{
	[[nodiscard]] inline bool Validate(uintptr_t a_target, std::span<const uint8_t> a_expected) noexcept
	{
		return !a_expected.empty() &&
			!std::memcmp(reinterpret_cast<const void*>(a_target), a_expected.data(), a_expected.size());
	}

	// nullopt takes the target's own byte
	[[nodiscard]] inline std::vector<uint8_t> GetWildcardSignature(
		uintptr_t a_target,
		std::span<const std::optional<uint8_t>> a_expected) noexcept
	{
		if (a_expected.empty())
			return {};

		std::vector<uint8_t> expected;
		expected.reserve(a_expected.size());

		const auto* target = reinterpret_cast<const uint8_t*>(a_target);
		for (size_t i = 0; i < a_expected.size(); i++)
			expected.push_back(a_expected[i].value_or(target[i]));

		return expected;
	}
}
