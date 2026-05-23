#include <AdConsoleHistory.h>
#include <AdUtils.h>

#include <toml11/single_include/toml.hpp>

#include <ShlObj_core.h>
#include <Windows.h>

#undef ERROR
#undef MAX_PATH

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <unordered_set>

namespace Addictol
{
	static REX::TOML::U32<> nConsoleHistorySize{ "Console"sv, "nConsoleHistorySize"sv, 128 };

	namespace
	{
		std::string TrimAscii(std::string_view a_s) noexcept
		{
			std::size_t b = 0;
			while (b < a_s.size() && std::isspace(static_cast<unsigned char>(a_s[b]))) ++b;
			std::size_t e = a_s.size();
			while (e > b && std::isspace(static_cast<unsigned char>(a_s[e - 1]))) --e;
			return std::string{ a_s.substr(b, e - b) };
		}

		std::string ToLowerAscii(std::string_view a_s) noexcept
		{
			std::string out;
			out.reserve(a_s.size());
			for (char c : a_s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return out;
		}

		bool IEqualsAscii(std::string_view a, std::string_view b) noexcept
		{
			if (a.size() != b.size()) return false;
			for (std::size_t i = 0; i < a.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a[i])) !=
				    std::tolower(static_cast<unsigned char>(b[i]))) return false;
			}
			return true;
		}

		// Split "alias_name [rest...]" - returns alias key + remainder.
		std::pair<std::string, std::string> SplitFirstToken(std::string_view a_s) noexcept
		{
			std::size_t i = 0;
			while (i < a_s.size() && std::isspace(static_cast<unsigned char>(a_s[i]))) ++i;
			std::size_t b = i;
			while (i < a_s.size() && !std::isspace(static_cast<unsigned char>(a_s[i]))) ++i;
			std::string key{ a_s.substr(b, i - b) };
			std::size_t r = i;
			while (r < a_s.size() && std::isspace(static_cast<unsigned char>(a_s[r]))) ++r;
			std::string rest = (r < a_s.size()) ? std::string{ a_s.substr(r) } : std::string{};
			return { key, rest };
		}
	}

	std::filesystem::path ConsoleHistory::HistoryPath() const noexcept
	{
		wchar_t* knownBuffer{ nullptr };
		const auto knownResult = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
		std::unique_ptr<wchar_t[], decltype(&CoTaskMemFree)> knownPath(knownBuffer, CoTaskMemFree);
		if (!knownPath || knownResult != 0) {
			return {};
		}
		std::filesystem::path path = knownPath.get();
		path /= std::format("My Games/{}/F4SE/Addictol"sv, GetSaveFolderName());
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		path /= "console_history.txt";
		return path;
	}

	void ConsoleHistory::Init() noexcept
	{
		bool expected = false;
		if (!initialized.compare_exchange_strong(expected, true)) return;

		LoadAliases();
		LoadFromDisk();

		worker = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
		REX::INFO("ConsoleHistory: initialized (in-memory={}, file={})"sv,
			Size(), HistoryPath().string());
	}

	void ConsoleHistory::Shutdown() noexcept
	{
		if (!initialized.load()) return;
		flushNow.store(true);
		workerCv.notify_all();
		if (worker.joinable()) {
			worker.request_stop();
			workerCv.notify_all();
			worker.join();
		}
		initialized.store(false);
	}

	void ConsoleHistory::LoadFromDisk() noexcept
	{
		auto path = HistoryPath();
		if (path.empty()) return;
		std::ifstream in{ path, std::ios::binary };
		if (!in.is_open()) return;
		std::uint32_t cap = nConsoleHistorySize.GetValue();
		if (cap == 0) cap = 128;
		std::lock_guard lk{ entriesMutex };
		entries.clear();
		std::string line;
		while (std::getline(in, line)) {
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
			if (line.empty()) continue;
			entries.push_back(std::move(line));
			while (entries.size() > cap) entries.pop_front();
			line.clear();
		}
	}

	void ConsoleHistory::WriteAtomic(const std::vector<std::string>& a_snapshot) const noexcept
	{
		auto path = HistoryPath();
		if (path.empty()) return;
		auto tmp = path;
		tmp += ".tmp";
		{
			std::ofstream out{ tmp, std::ios::binary | std::ios::trunc };
			if (!out.is_open()) return;
			for (const auto& s : a_snapshot) {
				out.write(s.data(), static_cast<std::streamsize>(s.size()));
				out.put('\n');
			}
		}
		std::error_code ec;
		std::filesystem::path src = tmp, dst = path;
		if (!MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			std::filesystem::remove(tmp, ec);
		}
	}

	void ConsoleHistory::WorkerLoop(std::stop_token a_stop) noexcept
	{
		using namespace std::chrono_literals;
		while (!a_stop.stop_requested()) {
			std::unique_lock lk{ workerMutex };
			workerCv.wait_for(lk, 500ms, [&]{
				return a_stop.stop_requested() || flushNow.load();
			});
			lk.unlock();
			if (!dirty.exchange(false)) {
				flushNow.store(false);
				if (a_stop.stop_requested()) break;
				continue;
			}
			flushNow.store(false);
			std::vector<std::string> snap;
			{
				std::lock_guard elk{ entriesMutex };
				snap.assign(entries.begin(), entries.end());
			}
			WriteAtomic(snap);
		}
		// Final flush.
		if (dirty.exchange(false)) {
			std::vector<std::string> snap;
			{
				std::lock_guard elk{ entriesMutex };
				snap.assign(entries.begin(), entries.end());
			}
			WriteAtomic(snap);
		}
	}

	void ConsoleHistory::RequestFlush() noexcept
	{
		dirty.store(true);
		workerCv.notify_all();
	}

	void ConsoleHistory::FlushSyncBlocking() noexcept
	{
		if (!initialized.load()) return;
		std::vector<std::string> snap;
		{
			std::lock_guard elk{ entriesMutex };
			snap.assign(entries.begin(), entries.end());
		}
		WriteAtomic(snap);
		dirty.store(false);
	}

	bool ConsoleHistory::GetEntry(std::size_t a_index, std::string& a_out) const noexcept
	{
		std::lock_guard lk{ entriesMutex };
		if (a_index >= entries.size()) return false;
		// 0 = newest = back.
		a_out = entries[entries.size() - 1 - a_index];
		return true;
	}

	std::size_t ConsoleHistory::Size() const noexcept
	{
		std::lock_guard lk{ entriesMutex };
		return entries.size();
	}

	void ConsoleHistory::Clear() noexcept
	{
		{
			std::lock_guard lk{ entriesMutex };
			entries.clear();
		}
		RequestFlush();
	}

	bool ConsoleHistory::ProcessCommand(std::string_view a_raw, std::string& a_outExpanded) noexcept
	{
		std::string trimmed = TrimAscii(a_raw);
		if (trimmed.empty()) {
			a_outExpanded = std::string{ a_raw };
			return true;
		}

		// `clear` intercept (case-insensitive).
		if (IEqualsAscii(trimmed, "clear")) {
			Clear();
			return false;
		}

		// Alias expansion (recursive, depth 8, cycle-protected).
		std::string expanded = trimmed;
		{
			std::lock_guard alk{ aliasMutex };
			if (!aliases.empty()) {
				std::unordered_set<std::string> visited;
				for (int depth = 0; depth < 8; ++depth) {
					auto [head, rest] = SplitFirstToken(expanded);
					if (head.empty()) break;
					std::string key = ToLowerAscii(head);
					auto it = aliases.find(key);
					if (it == aliases.end()) break;
					if (!visited.insert(key).second) {
						REX::WARN("ConsoleHistory: alias cycle detected at '{}'"sv, key);
						break;
					}
					expanded = it->second;
					if (!rest.empty()) {
						expanded.push_back(' ');
						expanded.append(rest);
					}
				}
			}
		}

		// Capture (filter: dedupe-of-last, empty).
		{
			std::lock_guard lk{ entriesMutex };
			std::uint32_t cap = nConsoleHistorySize.GetValue();
			if (cap == 0) cap = 128;
			if (!trimmed.empty() && (entries.empty() || entries.back() != trimmed)) {
				entries.push_back(trimmed);
				while (entries.size() > cap) entries.pop_front();
				dirty.store(true);
			}
		}
		workerCv.notify_all();

		a_outExpanded = std::move(expanded);
		return true;
	}

	void ConsoleHistory::LoadAliases() noexcept
	{
		auto parseFile = [this](const std::filesystem::path& a_path) {
			std::error_code ec;
			if (!std::filesystem::exists(a_path, ec)) return;
			auto result = toml::try_parse(a_path.string());
			if (result.is_err()) {
				REX::WARN("ConsoleHistory: failed to parse '{}' for aliases"sv, a_path.string());
				return;
			}
			const auto& root = result.unwrap();
			if (!root.contains("Console")) return;
			const auto& console = root.at("Console");
			if (!console.is_table() || !console.contains("Aliases")) return;
			const auto& aliasesTbl = console.at("Aliases");
			if (!aliasesTbl.is_table()) return;
			std::lock_guard alk{ aliasMutex };
			for (const auto& [k, v] : aliasesTbl.as_table()) {
				if (v.is_string()) {
					aliases[ToLowerAscii(k)] = v.as_string();
				}
			}
		};

		{
			std::lock_guard alk{ aliasMutex };
			aliases.clear();
		}

		auto runtimeDir = AdGetRuntimeDirectory();
		std::filesystem::path base = std::filesystem::path{ runtimeDir } / "Data/F4SE/Plugins";
		parseFile(base / "Addictol.toml");
		parseFile(base / "AddictolCustom.toml");

		std::size_t n{};
		{
			std::lock_guard alk{ aliasMutex };
			n = aliases.size();
		}
		REX::INFO("ConsoleHistory: loaded {} alias(es)"sv, n);
	}
}
