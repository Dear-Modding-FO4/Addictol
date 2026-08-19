#include <Modules/AdModuleSprintStutter.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesSprintStutter{ "Fixes"sv, "bSprintStutter"sv, true };

	ModuleSprintStutter::ModuleSprintStutter() :
		Module("Sprint Stutter", &bFixesSprintStutter)
	{}

	bool ModuleSprintStutter::DoQuery() const noexcept
	{
		if (IsModDLLPresent("SprintStutteringFix.dll"))
		{
			Skip("Standalone 'SprintStutteringFix.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleSprintStutter::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		static REL::Relocation<float*> CameraSnapThreshold{ REL::ID{ 61995, 2664490 } };
		if (*CameraSnapThreshold != 500.0f)
		{
			REX::WARN("Sprint Stutter: target constant is not 500.0f -- skipping to avoid corruption."sv);
			return false;
		}

		// FirstPersonState::Update camera snap threshold, 500.0f -> 1100.0f.
		*CameraSnapThreshold = 1100.0f;
		return true;
	}

}
