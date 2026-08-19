#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdTelemetry.h>

namespace Addictol
{
	class ModuleEscapeFreeze :
		public Module,
		public EscapeFreezeMetricSource
	{
	public:
		ModuleEscapeFreeze();
		virtual ~ModuleEscapeFreeze() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}