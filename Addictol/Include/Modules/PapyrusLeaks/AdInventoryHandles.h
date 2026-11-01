#pragma once

#include <Modules/PapyrusLeaks/AdPapyrusHandleKind.h>

namespace Addictol::PapyrusLeaks
{
	// (1 << 48) | (uniqueID << 32) | containerFormID
	struct InventoryHandles
	{
		static constexpr uint16_t kKind{ 1 };
		static constexpr std::string_view kName{ "inventory items" };

		[[nodiscard]] static std::string_view CheckSites() noexcept;
		[[nodiscard]] static bool Install() noexcept;
		static void RegisterSinks() noexcept;
		[[nodiscard]] static bool IsOrphan(uint64_t a_handle) noexcept;
		static void Release(uint64_t a_handle) noexcept;
	};
	static_assert(RegistersSinks<InventoryHandles> && SweepsOrphans<InventoryHandles>);
}
