#pragma once

#include <string_view>

namespace Addictol
{
	// Module profiling instrumentation for ModuleManager.
	// These free functions bracket SafeQueryMod/SafeInstallMod calls
	// to time each Addictol module's initialization phases.
	// All functions are no-ops when the profiler is inactive.

	void ProfilerBeginModuleQuery(std::string_view a_name) noexcept;
	void ProfilerEndModuleQuery(std::string_view a_name, bool a_success, bool a_skipped = false) noexcept;
	void ProfilerBeginModuleInstall(std::string_view a_name) noexcept;
	void ProfilerEndModuleInstall(std::string_view a_name, bool a_success, bool a_skipped = false) noexcept;

	// Submits any remaining pending module entries to ProfilerCore.
	// Call after all query/install phases complete as a safety flush.
	void ProfilerFlushModuleEntries() noexcept;
}
