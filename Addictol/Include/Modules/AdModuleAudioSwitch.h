#pragma once

#include <AdModule.h>
#include <AdTelemetry.h>

namespace Addictol
{
	class ModuleAudioSwitch :
		public Module,
		public AudioPerformanceMetricSource
	{
	public:
		ModuleAudioSwitch();
		virtual ~ModuleAudioSwitch() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
		[[nodiscard]] virtual bool HasProcessDefender() noexcept override;
	};
}
