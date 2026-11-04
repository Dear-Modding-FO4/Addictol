#include <Modules/AdModuleSlowPluginWarning.h>
#include <Telemetry/AdSlowHandlerLog.h>

namespace Addictol
{
	ModuleSlowPluginWarning::ModuleSlowPluginWarning() :
		Module("Slow Plugin Warning", &bWarningsSlowPlugin)
	{}

	bool ModuleSlowPluginWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		SlowHandlerLog::Get().LogStartupSummary();
		return true;
	}
}
