#pragma once

#include <concepts>
#include <stdint.h>
#include <string_view>

namespace Addictol
{
	// A Papyrus handle kind (the high word) whose bound scripts the engine leaks
	template<class T>
	concept PapyrusHandleKind = requires
	{
		{ T::kKind } -> std::convertible_to<uint16_t>;
		{ T::kName } -> std::convertible_to<std::string_view>;
		{ T::CheckSites() } noexcept -> std::same_as<std::string_view>;  // empty when patchable
		{ T::Install() } noexcept -> std::same_as<bool>;
	};

	// A kind that watches engine events once game data is ready
	template<class T>
	concept RegistersSinks = PapyrusHandleKind<T> && requires
	{
		{ T::RegisterSinks() } noexcept;
	};

	// A kind whose leaked scripts persist in saves and are swept after each load
	template<class T>
	concept SweepsOrphans = PapyrusHandleKind<T> && requires(uint64_t a_handle)
	{
		{ T::IsOrphan(a_handle) } noexcept -> std::same_as<bool>;
		{ T::Release(a_handle) } noexcept;
	};
}
