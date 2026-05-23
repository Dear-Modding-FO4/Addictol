#include <Modules/AdModuleConsoleFont.h>
#include <AdUtils.h>
#include <AdConsoleSubsystem.h>

namespace Addictol
{
	static REX::TOML::Bool<> bConsoleFontOverride{ "Console"sv, "bConsoleFontOverride"sv, false };
	static REX::TOML::I32<>  nConsoleFontSize{ "Console"sv, "nConsoleFontSize"sv, 14 };

	namespace consoleFontDetail
	{
		static void HandleOpenClose(bool a_opening) noexcept
		{
			if (!a_opening) return;
			std::int32_t size = nConsoleFontSize.GetValue();
			if (size <= 0) return;
			(void)ConsoleSubsystem::GetSingleton()->ApplyOutputFontSize(size);
		}
	}

	ModuleConsoleFont::ModuleConsoleFont() :
		Module("Console Font", &bConsoleFontOverride)
	{}

	bool ModuleConsoleFont::DoQuery() const noexcept
	{
		return RE::VTABLE::Console[1].address() != 0;
	}

	bool ModuleConsoleFont::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		ConsoleSubsystem::GetSingleton()->AddOpenCloseCallback(&consoleFontDetail::HandleOpenClose);
		REX::INFO("Console Font: registered (apply size {} on open)"sv, nConsoleFontSize.GetValue());
		return true;
	}

	bool ModuleConsoleFont::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleConsoleFont::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
