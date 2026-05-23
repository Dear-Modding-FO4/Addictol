#include <Modules/AdModuleConsoleSubsystem.h>
#include <AdUtils.h>

#include <AdConsoleSubsystem.h>

#include <RE/C/Console.h>

namespace Addictol
{
	static REX::TOML::Bool<> bConsoleSubsystem{ "Console"sv, "bConsoleSubsystem"sv, true };

	ModuleConsoleSubsystem::ModuleConsoleSubsystem() :
		Module("Console Subsystem", &bConsoleSubsystem)
	{}

	bool ModuleConsoleSubsystem::DoQuery() const noexcept
	{
		const auto vtable = RE::VTABLE::Console[1].address();
		if (vtable == 0) {
			REX::ERROR("ModuleConsoleSubsystem: Console vtable[1] unresolved; disabling substrate"sv);
			return false;
		}
		return true;
	}

	bool ModuleConsoleSubsystem::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
			return ConsoleSubsystem::GetSingleton()->RegisterMenuSink();
		}
		return ConsoleSubsystem::GetSingleton()->InstallVTableHooks();
	}

	bool ModuleConsoleSubsystem::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleConsoleSubsystem::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
