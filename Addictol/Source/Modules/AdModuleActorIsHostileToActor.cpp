#include <Modules\AdModuleActorIsHostileToActor.h>
#include <AdUtils.h>
#include <RE/A/Actor.h>
#include <RE/G/GameScript.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesActorIsHostileToActor{ "Patches", "bActorIsHostileToActor", true };

	inline bool IsHostileToActor(RE::BSScript::IVirtualMachine* a_vm, std::uint32_t a_stackID, RE::Actor* a_self, RE::Actor* a_actor)
	{
		if (!a_actor)
		{
			// FIXME: No works in OG
			RE::GameScript::LogFormError(a_actor, "Cannot call IsHostileToActor with a None actor", a_vm, a_stackID);
			return false;
		}
		else
		{
			return a_self->GetHostileToActor(a_actor);
		}
	}

	ModuleActorIsHostileToActor::ModuleActorIsHostileToActor() :
		Module("Module ActorIsHostileToActor", &bPathesActorIsHostileToActor)
	{}

	bool ModuleActorIsHostileToActor::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleActorIsHostileToActor::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target;
		std::size_t Size = 0x10;

		if (RELEX::IsRuntimeAE())
		{
			// AE
			Target = REL::ID(4486975);
		}
		else if (RELEX::IsRuntimeNG())
		{
			// NG, no ID for this Function
			Target = REL::Offset(0x1081B20);
		}
		else
		{
			// OG
			Target = REL::ID(1432894);
		}

		REL::WriteSafeFill(Target.address(), REL::INT3, Size);
		RELEX::DetourJump(Target.address(), reinterpret_cast<std::uintptr_t>(IsHostileToActor));

		return true;
	}

	bool ModuleActorIsHostileToActor::DoListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}