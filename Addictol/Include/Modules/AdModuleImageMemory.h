#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdImageMemory.h>

namespace Addictol
{
	class ModuleImageMemory : public Module, public ImageMemory
	{
	public:
		ModuleImageMemory();
		[[nodiscard]] bool DoInstall(F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
