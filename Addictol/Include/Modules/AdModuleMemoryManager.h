#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdTelemetry.h>

#include <atomic>

namespace Addictol
{
	class ModuleMemoryManager :
		public Module,
		public MetricSource
	{
	public:
		ModuleMemoryManager();
		virtual ~ModuleMemoryManager() = default;

		[[nodiscard]] virtual bool DoQuery() const noexcept override;
		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override;

		std::atomic<bool> m_active{ false };
	};
}