#pragma once

#include <atomic>

#include <AdProfilerCore.h>

namespace Addictol
{
	using namespace std::literals;

	class ProfilerBA2 :
		public REX::Singleton<ProfilerBA2>
	{
		std::atomic<std::uint64_t> m_chunkCounter{ 0 };

	public:
		// Calls may arrive concurrently from BA2 decompression workers.
		void RecordDecompression(std::size_t a_compressedSize,
			std::size_t a_uncompressedSize,
			double a_elapsedMs) noexcept;
	};
}
