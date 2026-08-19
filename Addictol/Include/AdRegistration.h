#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol
{
	enum class Registration : uint32_t
	{
		kAccepted,
		kNullCallback,
		kInvalidName,
		kDuplicate,
		kFull,
		kClosed
	};

	[[nodiscard]] constexpr std::string_view Describe(Registration a_result) noexcept
	{
		switch (a_result)
		{
		case Registration::kAccepted:
			return "accepted";
		case Registration::kNullCallback:
			return "callback is null";
		case Registration::kInvalidName:
			return "name is empty or too long";
		case Registration::kDuplicate:
			return "name is already registered";
		case Registration::kFull:
			return "no free slot";
		default:
			return "registration is closed";
		}
	}

	inline constexpr size_t kNameCapacity = 32;

	[[nodiscard]] constexpr bool ValidRegistrationName(std::string_view a_name) noexcept
	{
		return !a_name.empty() && a_name.size() < kNameCapacity;
	}

	// copied name outlives the call
	constexpr void CopyRegistrationName(char (&a_storage)[kNameCapacity], std::string_view a_name) noexcept
	{
		const auto count = a_name.size() < kNameCapacity ? a_name.size() : kNameCapacity - 1;
		for (size_t index = 0; index < count; ++index)
			a_storage[index] = a_name[index];
		a_storage[count] = '\0';
	}
}
