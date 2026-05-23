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
	// Persistent console history: bounded deque, debounced disk flush, alias expansion from [Console.Aliases].
	class ConsoleHistory :
		public REX::TSingleton<ConsoleHistory>
	{
	public:
		// Lifecycle.
		void Init() noexcept;       // load file + spin worker; idempotent
		void Shutdown() noexcept;   // flush + stop worker; idempotent

		// Returns false to intercept (currently `clear` only); true to forward a_outExpanded to the engine.
		[[nodiscard]] bool ProcessCommand(std::string_view a_command, std::string& a_outExpanded) noexcept;

		// a_index 0 = newest; false past the end.
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
