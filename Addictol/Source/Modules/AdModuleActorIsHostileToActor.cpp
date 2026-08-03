#include <Modules/AdModuleActorIsHostileToActor.h>
#include <AdUtils.h>

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

	bool ModuleActorIsHostileToActor::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleActorIsHostileToActor::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation{ REL::ID{ 1022223, 0, 4486975 }, REL::Offset{ 0x0, 0x1081B20, 0x0 } }.address(); // NG has no ID
		const size_t size = 0x10;

		if (!RELEX::Validate(target, { 0x49, 0x8B, 0xD1, 0x49, 0x8B, 0xC8, 0xE9 }))
			return false;

		REL::WriteSafeFill(target, REL::INT3, size);
		return RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&actorIsHostileToActorDetail::IsHostileToActor)) != 0;
	}

	bool ModuleActorIsHostileToActor::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleActorIsHostileToActor::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}