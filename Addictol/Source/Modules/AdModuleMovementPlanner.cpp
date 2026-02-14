#include <Modules\AdModuleMovementPlanner.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMovementPlanner{ "Fixes"sv, "bMovementPlanner"sv, true };

	struct CanWarpOnPathFailure
	{
		static bool thunk(const RE::Actor* a_actor)
		{
			return a_actor ? func(a_actor) : true;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	ModuleMovementPlanner::ModuleMovementPlanner() :
		Module("Movement Planner", &bFixesMovementPlanner)
	{}

	bool ModuleMovementPlanner::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMovementPlanner::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target{ REL::ID{ 1403049, 2234683 }, 0x30 };

		auto& trampoline = REL::GetTrampoline();
		CanWarpOnPathFailure::func = trampoline.write_call<5>(Target.address(), CanWarpOnPathFailure::thunk);

		return true;
	}

	bool ModuleMovementPlanner::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMovementPlanner::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}