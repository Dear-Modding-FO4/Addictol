#pragma once

#include <Modules/PapyrusLeaks/AdPapyrusHandleKind.h>

namespace Addictol::PapyrusLeaks
{
	// (aliasID or 0xFFFF) << 32 | formID; native-type Objects bound to plugin forms
	struct NativeWrappers
	{
		static constexpr uint16_t kKind{ 0 };
		static constexpr std::string_view kName{ "native form wrappers" };

		[[nodiscard]] static std::string_view CheckSites() noexcept;
		[[nodiscard]] static bool Install() noexcept;
	};
	static_assert(PapyrusHandleKind<NativeWrappers>);
}
