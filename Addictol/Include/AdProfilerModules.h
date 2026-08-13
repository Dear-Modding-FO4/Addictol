#pragma once

#include <string_view>

namespace Addictol
{
	// These hooks are no-ops while profiling is inactive.

	void ProfilerBeginModuleQuery(std::string_view a_name) noexcept;
	void ProfilerEndModuleQuery(std::string_view a_name, bool a_success, bool a_skipped = false) noexcept;
	void ProfilerBeginModuleInstall(std::string_view a_name) noexcept;
	void ProfilerEndModuleInstall(std::string_view a_name, bool a_success, bool a_skipped = false) noexcept;

	void ProfilerFlushModuleEntries() noexcept;
}
