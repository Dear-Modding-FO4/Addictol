#include <Modules/AdModuleMovementPlanner.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMovementPlanner{ "Fixes"sv, "bMovementPlanner"sv, true };

	namespace movementPlannerDetail
	{
		struct CanWarpOnPathFailure
		{
			static bool thunk(const RE::Actor* a_actor)
			{
				return a_actor ? func(a_actor) : true;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleMovementPlanner::ModuleMovementPlanner() :
		Module("Movement Planner", &bFixesMovementPlanner)
	{}

	bool ModuleMovementPlanner::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace movementPlannerDetail;
		REL::Relocation Target{ REL::ID{ 1403049, 2234683 }, 0x30 };
		CanWarpOnPathFailure::func = RELEX::DetourClassCall(Target, &CanWarpOnPathFailure::thunk);
		return CanWarpOnPathFailure::func != 0;
	}

}