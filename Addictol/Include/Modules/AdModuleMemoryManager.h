#pragma once

#include <Core/AdModule.h>
#include <Memory/Heaps/AdHeapBackend.h>
#include <Telemetry/AdTelemetry.h>

#include <array>
#include <atomic>

namespace Addictol
{
	class ModuleMemoryManager :
		public Module,
		public MetricSource,
		public SeriesSource
	{
	public:
		ModuleMemoryManager();
		virtual ~ModuleMemoryManager() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;
		[[nodiscard]] size_t SeriesCapacity() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;
		[[nodiscard]] size_t DrainSeries(std::span<SeriesSample> a_out) noexcept override;
		static void InstallReplacementHeap(uintptr_t a_base) noexcept;

		std::atomic<bool> m_active{ false };
		// Telemetry worker only: the previous cumulative class counters, for interval deltas.
		std::array<HeapClassStatistics, kMaxHeapClasses> m_previousClasses{};
	};
}