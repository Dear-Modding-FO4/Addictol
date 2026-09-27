#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdImageSampling.h>

namespace Addictol
{
	class ModuleImageSampling : public Module, public ImageSampling
	{
	public:
		ModuleImageSampling();
		[[nodiscard]] bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
