#pragma once

#include <AdModule.h>
#include <AdTelemetry.h>

namespace Addictol
{
	class ModuleReferenceHandleLimitWarning :
		public Module,
		public ReferenceHandleMetricSource
	{
	public:
		ModuleReferenceHandleLimitWarning();
		virtual ~ModuleReferenceHandleLimitWarning() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}