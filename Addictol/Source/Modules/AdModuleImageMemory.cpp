#include <Modules/AdModuleImageMemory.h>
#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	ModuleImageMemory::ModuleImageMemory() :
		Module("Image Memory", &bTelemetryImageMemory)
	{}

	bool ModuleImageMemory::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return Start();
	}
}
