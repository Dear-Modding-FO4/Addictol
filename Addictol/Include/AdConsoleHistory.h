#pragma once

#include <REX/REX.h>
#include <F4SE/F4SE.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace Addictol
{
	// ConsoleHistory - persistent console history service.
	// - In-memory ring buffer of UTF-8 command lines (oldest at front, newest at back).
	// - Bounded by nConsoleHistorySize.
	// - Persisted to <Documents>/My Games/<SaveFolder>/F4SE/Addictol/console_history.txt
	//   on a background worker thread, debounced 500ms.
	// - Alias expansion table loaded from [Console.Aliases] in Addictol.toml +
	//   AddictolCustom.toml. Aliases expand recursively up to depth 8.
	class ConsoleHistory :
		public REX::TSingleton<ConsoleHistory>
	{
	public:
		// Lifecycle.
		void Init() noexcept;       // load file + spin worker; idempotent
		void Shutdown() noexcept;   // flush + stop worker; idempotent

		// Capture path: called from ExecuteCommand hook BEFORE invoking original.
		// Returns true if the command should still run, false if intercepted
		// (currently only "clear" is intercepted - returns false).
		// On true return, a_command may have been alias-expanded; caller passes
		// a_outExpanded to the original function instead of the raw input.
		[[nodiscard]] bool ProcessCommand(std::string_view a_command, std::string& a_outExpanded) noexcept;

		// Recall API for Up/Down handler.
		// a_index 0 = newest entry, increments toward older. Returns false when
		// past the end. Pointers stable until next Append/Clear.
		[[nodiscard]] bool GetEntry(std::size_t a_index, std::string& a_out) const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;

		void Clear() noexcept;
		void RequestFlush() noexcept;       // wake the worker for a debounced flush
		void FlushSyncBlocking() noexcept;  // synchronously persist now

		void LoadAliases() noexcept;        // re-read both TOMLs

	private:
		std::filesystem::path HistoryPath() const noexcept;
		void LoadFromDisk() noexcept;
		void WorkerLoop(std::stop_token a_stop) noexcept;
		void WriteAtomic(const std::vector<std::string>& a_snapshot) const noexcept;

		// In-memory store.
		mutable std::mutex      entriesMutex;
		std::deque<std::string> entries;

		// Aliases.
		mutable std::mutex                                  aliasMutex;
		std::unordered_map<std::string, std::string>        aliases;

		// Worker.
		std::jthread             worker;
		std::mutex               workerMutex;
		std::condition_variable  workerCv;
		std::atomic<bool>        dirty{ false };
		std::atomic<bool>        flushNow{ false };

		std::atomic<bool>        initialized{ false };
	};
}
