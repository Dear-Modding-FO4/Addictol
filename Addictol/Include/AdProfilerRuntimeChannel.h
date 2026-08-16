#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Addictol
{
	using namespace std::literals;

	struct RuntimeRowMetadata
	{
		std::string_view sessionID;
		uint64_t saveLoadEpoch{ 0 };
		uint64_t monotonicUs{ 0 };
		uint64_t channelSequence{ 0 };
	};

	class RuntimeSessionContext
	{
		std::chrono::steady_clock::time_point m_startTime;
		std::string m_sessionID;
		std::string m_outputDirectory;
		std::atomic<uint64_t> m_saveLoadEpoch{ 0 };

	public:
		void Start(std::string a_sessionID, std::string a_outputDirectory) noexcept
		{
			m_startTime = std::chrono::steady_clock::now();
			m_sessionID = std::move(a_sessionID);
			m_outputDirectory = std::move(a_outputDirectory);
			m_saveLoadEpoch.store(0, std::memory_order_release);
		}

		void AdvanceSaveLoadEpoch() noexcept
		{
			m_saveLoadEpoch.fetch_add(1, std::memory_order_acq_rel);
		}

		[[nodiscard]] RuntimeRowMetadata Capture() const noexcept
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - m_startTime);
			return {
				m_sessionID,
				m_saveLoadEpoch.load(std::memory_order_acquire),
				static_cast<uint64_t>(elapsed.count()),
				0
			};
		}

		[[nodiscard]] const std::string& GetSessionID() const noexcept { return m_sessionID; }
		[[nodiscard]] const std::string& GetOutputDirectory() const noexcept { return m_outputDirectory; }
	};

	inline void WriteRuntimeCSVMetadataHeader(std::ostream& a_file)
	{
		a_file << "SessionId,SaveLoadEpoch,MonotonicUs,ChannelSequence,"sv;
	}

	inline void WriteRuntimeCSVMetadata(std::ostream& a_file, const RuntimeRowMetadata& a_metadata)
	{
		a_file << a_metadata.sessionID << ","sv
			<< a_metadata.saveLoadEpoch << ","sv
			<< a_metadata.monotonicUs << ","sv
			<< a_metadata.channelSequence << ","sv;
	}

	class RuntimeCsvFile
	{
	public:
		using HeaderWriter = void(*)(std::ostream&);

		RuntimeCsvFile(
			RuntimeSessionContext& a_session,
			std::string_view a_fileStem,
			HeaderWriter a_headerWriter) noexcept :
			m_session(a_session),
			m_fileStem(a_fileStem),
			m_headerWriter(a_headerWriter)
		{}

		RuntimeCsvFile(const RuntimeCsvFile&) = delete;
		RuntimeCsvFile& operator=(const RuntimeCsvFile&) = delete;

		[[nodiscard]] std::ostream* Begin() noexcept
		{
			if (!Open())
			{
				++m_failures;
				return nullptr;
			}
			return &m_stream;
		}

		void End() noexcept
		{
			m_stream.flush();
			if (!m_stream.good())
			{
				++m_failures;
				m_stream.close();
				m_stream.clear();
			}
		}

		[[nodiscard]] uint64_t GetFailureCount() const noexcept { return m_failures; }
		[[nodiscard]] const std::string& GetPath() const noexcept { return m_path; }

	private:
		[[nodiscard]] bool Open() noexcept
		{
			if (m_stream.is_open() && m_stream.good())
				return true;
			if (m_stream.is_open())
				m_stream.close();
			m_stream.clear();

			if (m_path.empty())
			{
				const auto& directory = m_session.GetOutputDirectory();
				if (directory.empty())
					return false;
				std::error_code ec;
				std::filesystem::create_directories(directory, ec);
				if (ec)
					return false;

				auto now = std::time(nullptr);
				std::tm tm{};
				localtime_s(&tm, &now);
				char timeBuf[64];
				std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tm);
				m_path = std::format("{}{}_{}.csv"sv, directory, m_fileStem, timeBuf);
			}

			m_stream.open(m_path, m_headerWritten ? std::ios::app : std::ios::out);
			if (!m_stream.is_open())
				return false;
			if (!m_headerWritten)
			{
				m_headerWriter(m_stream);
				m_stream.flush();
				if (!m_stream.good())
				{
					m_stream.close();
					m_stream.clear();
					return false;
				}
				m_headerWritten = true;
			}
			return true;
		}

		RuntimeSessionContext& m_session;
		std::string_view m_fileStem;
		HeaderWriter m_headerWriter;
		std::string m_path;
		std::ofstream m_stream;
		uint64_t m_failures{ 0 };
		bool m_headerWritten{ false };
	};

	template <class T>
	class RuntimeChannel
	{
	public:
		using HeaderWriter = RuntimeCsvFile::HeaderWriter;
		using EntryWriter = void(*)(std::ostream&, const T&, const RuntimeRowMetadata&);

		RuntimeChannel(
			RuntimeSessionContext& a_session,
			size_t a_capacity,
			std::string_view a_fileStem,
			HeaderWriter a_headerWriter,
			EntryWriter a_entryWriter) noexcept :
			m_session(a_session),
			m_capacity(a_capacity),
			m_file(a_session, a_fileStem, a_headerWriter),
			m_entryWriter(a_entryWriter)
		{}

		RuntimeChannel(const RuntimeChannel&) = delete;
		RuntimeChannel& operator=(const RuntimeChannel&) = delete;

		void Record(T&& a_entry, bool a_exportCSV) noexcept
		{
			std::optional<T> exportEntry;
			RuntimeRowMetadata metadata;
			if (a_exportCSV)
			{
				metadata = m_session.Capture();
				exportEntry.emplace(a_entry);
			}

			{
				std::lock_guard lock(m_entriesMutex);
				if (m_capacity)
				{
					if (m_entries.size() >= m_capacity)
						m_entries.pop_front();
					m_entries.push_back(std::move(a_entry));
				}
			}

			if (exportEntry)
				AppendCSV(*exportEntry, metadata);
		}

		// Readers copy retained entries; nothing is drained and no CSV sequence advances.
		[[nodiscard]] bool CopyLatest(T& a_out) const noexcept
		{
			std::lock_guard lock(m_entriesMutex);
			if (m_entries.empty())
				return false;

			a_out = m_entries.back();
			return true;
		}

		void CopyEntries(std::vector<T>& a_out) const noexcept
		{
			std::lock_guard lock(m_entriesMutex);
			a_out.assign(m_entries.begin(), m_entries.end());
		}

		template <class Summary, class Projection>
		[[nodiscard]] bool CopyLatestAndProject(
			T& a_latest,
			std::vector<Summary>& a_summaries,
			const Projection& a_projection) const noexcept
		{
			std::lock_guard lock(m_entriesMutex);
			if (m_entries.empty())
			{
				a_summaries.clear();
				return false;
			}

			a_latest = m_entries.back();
			a_summaries.resize(m_entries.size());
			for (size_t index = 0; index < m_entries.size(); ++index)
				a_summaries[index] = a_projection(m_entries[index]);
			return true;
		}

		[[nodiscard]] size_t RetainedCount() const noexcept
		{
			std::lock_guard lock(m_entriesMutex);
			return m_entries.size();
		}

	private:
		void AppendCSV(const T& a_entry, RuntimeRowMetadata a_metadata) noexcept
		{
			std::lock_guard lock(m_streamMutex);
			a_metadata.channelSequence = m_nextSequence++;
			auto* stream = m_file.Begin();
			if (!stream)
				return;

			m_entryWriter(*stream, a_entry, a_metadata);
			m_file.End();
		}

		RuntimeSessionContext& m_session;
		size_t m_capacity;
		RuntimeCsvFile m_file;
		EntryWriter m_entryWriter;
		std::deque<T> m_entries;
		mutable std::mutex m_entriesMutex;
		std::mutex m_streamMutex;
		uint64_t m_nextSequence{ 0 };
	};
}
