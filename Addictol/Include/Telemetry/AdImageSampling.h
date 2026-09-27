#pragma once

#include <Telemetry/AdImageRegistry.h>
#include <Telemetry/AdImageSeries.h>

namespace Addictol
{
	class ImageSampling : public ImageSeriesSource<3, true>
	{
	public:
		struct Thread
		{
			HANDLE handle{};
			DWORD id{};
			uintptr_t stackLow{}, stackHigh{};
			uint64_t cycles{};
		};
		struct Sample
		{
			ImageId image{ kInvalidImage };
			ImageId leaf{ kInvalidImage };
			uint64_t suspendedTicks{};
			bool suspendFailed{};
			bool fault{};
		};

		ImageSampling() noexcept;
		[[nodiscard]] bool Start(uint32_t a_hz, DWORD a_mainThread) noexcept;
		[[nodiscard]] static bool ReadThread(DWORD a_id, Thread& a_thread) noexcept;
		[[nodiscard]] static Sample Capture(const Thread& a_thread, const ImageRegistry& a_registry,
			bool a_forceFault = false) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		static DWORD WINAPI Worker(void* a_context) noexcept;
		static ImageId Walk(CONTEXT& a_context, const Thread& a_thread, const ImageRegistry& a_registry,
			std::span<uintptr_t> a_copy, bool a_forceFault) noexcept;
		void Refresh() noexcept;
		void Tick() noexcept;
		void Drain(std::span<MetricValue> a_out) noexcept override;
		[[nodiscard]] double TickScale() const noexcept override;

		std::array<Thread, 256> m_threads{};
		size_t m_threadCount{};
		DWORD m_mainThread{}, m_samplerThread{};
		HANDLE m_timer{};
		uint64_t m_periodTicks{};
		// TSC and QPC at start convert thread cycle counts into QPC time.
		uint64_t m_startTsc{}, m_startQpc{};
		uint32_t m_hz{};
		std::atomic<bool> m_started{};
		std::atomic<uint64_t> m_suspendFailures{}, m_maxSuspendedTicks{}, m_walkFaults{};
	};
}
