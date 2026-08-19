#include <Modules/AdModuleToggleGrassCommand.h>
#include <AdUtils.h>

#include <RE/S/SCRIPT_FUNCTION.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesToggleGrassCommand{ "Fixes"sv, "bToggleGrassCommand"sv, true };

	ModuleToggleGrassCommand::ModuleToggleGrassCommand() :
		Module("Toggle Grass Command", &bFixesToggleGrassCommand)
	{}

	bool ModuleToggleGrassCommand::DoQuery() const noexcept
	{
		// Only needed on AE
		return RELEX::IsRuntimeAE();
	}

	bool ModuleToggleGrassCommand::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RE::SCRIPT_FUNCTION* consoleCommand = RE::SCRIPT_FUNCTION::LocateConsoleCommand("ToggleGrass"sv);
		if (consoleCommand)
		{
			bool isReferenceFunction = false;
			REL::WriteSafe(reinterpret_cast<std::uintptr_t>(&consoleCommand->referenceFunction), &isReferenceFunction, sizeof(isReferenceFunction));
		}

		return true;
	}

}