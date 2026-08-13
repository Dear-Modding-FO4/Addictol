#include <AdProfilerBA2.h>

namespace Addictol
{
	void ProfilerBA2::RecordDecompression(std::size_t a_compressedSize,
		std::size_t a_uncompressedSize,
		double a_elapsedMs) noexcept
	{
		auto* profiler = ProfilerCore::GetSingleton();
		if (!profiler || !profiler->IsActive())
			return;

		double throughputMBps = 0.0;
		if (a_elapsedMs > 0.0)
		{
			double uncompressedMB = static_cast<double>(a_uncompressedSize) / (1024.0 * 1024.0);
			double elapsedSec = a_elapsedMs / 1000.0;
			throughputMBps = uncompressedMB / elapsedSec;
		}

		auto chunkId = m_chunkCounter.fetch_add(1, std::memory_order_relaxed);

		BA2ProfileEntry entry;
		entry.archiveName = std::format("chunk_{}"sv, std::to_string(chunkId));
		entry.decompressMs = a_elapsedMs;
		entry.compressedSize = a_compressedSize;
		entry.uncompressedSize = a_uncompressedSize;
		entry.throughputMBps = throughputMBps;

		profiler->AddBA2Entry(std::move(entry));
	}
}
