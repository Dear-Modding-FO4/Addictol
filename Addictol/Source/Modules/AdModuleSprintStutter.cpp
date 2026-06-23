#include <Modules/AdModuleSprintStutter.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesSprintStutter{ "Fixes"sv, "bSprintStutter"sv, true };

	ModuleSprintStutter::ModuleSprintStutter() :
		Module("Sprint Stutter", &bFixesSprintStutter)
	{}

	bool ModuleSprintStutter::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleSprintStutter::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// FirstPersonState::Update camera snap threshold, 500.0f -> 1100.0f.
		const auto target = REL::Relocation<std::uintptr_t>{ REL::ID{ 61995, 2664490, 2664490 } }.address();
		if (*reinterpret_cast<const std::uint32_t*>(target) != 0x43FA0000)
		{
			REX::WARN("Sprint Stutter: target constant is not 500.0f -- skipping to avoid corruption."sv);
			return false;
		}

		RELEX::WriteSafe(target, { 0x00, 0x80, 0x89, 0x44 });	// 1100.0f
		return true;
	}

	bool ModuleSprintStutter::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleSprintStutter::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
