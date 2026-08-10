#include <AdProfilerModules.h>
#include <AdProfilerCore.h>

#include <chrono>
#include <string>
#include <unordered_map>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		// Tracks the start time of the current operation per module name.
		// Module init is single-threaded (main thread), so no synchronization needed.
		std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> s_startTimes;

		// Accumulates profiling data for modules between query and install phases.
		// Entries are held here after query completes and submitted to ProfilerCore
		// once install completes (or immediately if query fails).
		std::unordered_map<std::string, ModuleProfileEntry> s_pendingEntries;
	}

	void ProfilerBeginModuleQuery(std::string_view a_name) noexcept
	{
		if (!ProfilerCore::GetSingleton()->IsActive() || !ProfilerCore::IsModuleProfilingEnabled())
			return;

		s_startTimes[std::string(a_name)] = std::chrono::high_resolution_clock::now();
	}

	void ProfilerEndModuleQuery(std::string_view a_name, bool a_success, bool a_skipped) noexcept
	{
		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive() || !ProfilerCore::IsModuleProfilingEnabled())
			return;

		std::string name(a_name);
		auto it = s_startTimes.find(name);
		if (it == s_startTimes.end())
			return;

		auto elapsed = std::chrono::duration<double, std::milli>(
			std::chrono::high_resolution_clock::now() - it->second).count();
		s_startTimes.erase(it);

		auto& entry = s_pendingEntries[name];
		entry.moduleName = name;
		entry.queryMs = elapsed;
		entry.querySuccess = a_success;
		entry.skipped = a_skipped;

		// If query failed, the module won't be installed - submit immediately
		if (!a_success)
		{
			profiler->AddModuleEntry(std::move(entry));
			s_pendingEntries.erase(name);
		}
	}

	void ProfilerBeginModuleInstall(std::string_view a_name) noexcept
	{
		if (!ProfilerCore::GetSingleton()->IsActive() || !ProfilerCore::IsModuleProfilingEnabled())
			return;

		s_startTimes[std::string(a_name)] = std::chrono::high_resolution_clock::now();
	}

	void ProfilerEndModuleInstall(std::string_view a_name, bool a_success, bool a_skipped) noexcept
	{
		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive() || !ProfilerCore::IsModuleProfilingEnabled())
			return;

		std::string name(a_name);
		auto it = s_startTimes.find(name);
		if (it == s_startTimes.end())
			return;

		auto elapsed = std::chrono::duration<double, std::milli>(
			std::chrono::high_resolution_clock::now() - it->second).count();
		s_startTimes.erase(it);

		auto& entry = s_pendingEntries[name];
		entry.moduleName = name;
		entry.installMs = elapsed;
		entry.installSuccess = a_success;
		entry.skipped = a_skipped;

		// Install is the final phase - submit the completed entry
		profiler->AddModuleEntry(std::move(entry));
		s_pendingEntries.erase(name);
	}

	void ProfilerFlushModuleEntries() noexcept
	{
		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive())
			return;

		for (auto& [name, entry] : s_pendingEntries)
			profiler->AddModuleEntry(std::move(entry));

		s_pendingEntries.clear();
		s_startTimes.clear();
	}
}
