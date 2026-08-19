#include <Modules/AdModuleActorIsHostileToActor.h>
#include <Core/AdUtils.h>

#include <RE/A/Actor.h>
#include <RE/G/GameScript.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesActorIsHostileToActor{ "Fixes"sv, "bActorIsHostileToActor"sv, true };

	namespace actorIsHostileToActorDetail
	{
		[[nodiscard]] inline static bool IsHostileToActor(RE::BSScript::IVirtualMachine* a_vm, std::uint32_t a_stackID,
			RE::Actor* a_self, RE::Actor* a_actor) noexcept
		{
			if (!a_actor)
			{
				RE::GameScript::LogFormError(a_actor, "Cannot call IsHostileToActor with a None actor", a_vm, a_stackID, RE::BSScript::ErrorLogger::Severity::kError);
				return false;
			}
			else
			{
				return a_self->GetHostileToActor(a_actor);
			}
		}
	}

	ModuleActorIsHostileToActor::ModuleActorIsHostileToActor() :
		Module("ActorIsHostileToActor", &bFixesActorIsHostileToActor)
	{}

	bool ModuleActorIsHostileToActor::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// NG has no ID for this site, so it anchors 0x10 off the preceding one.
		const auto target = REL::Relocation{ REL::ID{ 1022223, 2251858, 4486975 }, REL::Offset{ 0x0, 0x10, 0x0 } }.address();
		const size_t size = 0x10;

		if (!RELEX::Validate(target, { 0x49, 0x8B, 0xD1, 0x49, 0x8B, 0xC8, 0xE9 }))
			return false;

		REL::WriteSafeFill(target, REL::INT3, size);
		return RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&actorIsHostileToActorDetail::IsHostileToActor)) != 0;
	}

}