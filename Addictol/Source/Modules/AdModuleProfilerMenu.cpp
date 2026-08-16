#include <Modules/AdModuleProfilerMenu.h>
#include <AdProfilerCore.h>
#include <AdProfilerMenu.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bProfilerMenu{ "Profiler"sv, "bProfilerMenu"sv, false };

	ModuleProfilerMenu::ModuleProfilerMenu() :
		Module("Profiler Menu", &bProfilerMenu)
	{}

	bool ModuleProfilerMenu::DoQuery() const noexcept
	{
		// The menu is a viewer: it never enables the profiler or any recorder on its own.
		if (!ProfilerCore::IsEnabledInConfig())
		{
			Skip("bProfilerMenu requires bProfiler = true; nothing is installed and no hook is written"sv);
			return false;
		}

		return true;
	}

	bool ModuleProfilerMenu::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Sinks are permanent and must register before the platform installs its hooks.
		return ProfilerMenu::Install();
	}

	bool ModuleProfilerMenu::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleProfilerMenu::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
