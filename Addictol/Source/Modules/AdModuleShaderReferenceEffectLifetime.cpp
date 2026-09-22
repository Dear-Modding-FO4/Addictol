#include <Modules/AdModuleShaderReferenceEffectLifetime.h>
#include <Core/AdDeferredOwnerRegistry.h>
#include <Core/AdUtils.h>

#include <RE/A/ActiveEffect.h>
#include <RE/A/ActiveEffectReferenceEffectController.h>
#include <RE/B/BSTSmartPointer.h>
#include <RE/R/ReferenceEffectController.h>
#include <RE/RTTI.h>
#include <RE/S/ShaderReferenceEffect.h>

namespace Addictol
{
	namespace shaderReferenceEffectLifetimeDetail
	{
		using Owner = RE::BSTSmartPointer<RE::ActiveEffect>;
		using Registry = DeferredOwnerRegistry<RE::ShaderReferenceEffect*, Owner>;
		using Constructor = RE::ShaderReferenceEffect* (__fastcall*)(
			RE::ShaderReferenceEffect*, RE::ReferenceEffectController*);
		using DeletingDestructor = void* (__fastcall*)(RE::ShaderReferenceEffect*, uint32_t);

		static inline Constructor s_constructor{};
		static inline DeletingDestructor s_deletingDestructor{};
		static inline uintptr_t s_activeEffectControllerVtable{};

		[[nodiscard]] static Registry& GetRegistry()
		{
			// Exit-only queued pins stay retained instead of releasing engine objects during static teardown.
			static auto* registry = new Registry;
			return *registry;
		}

		static void QueueRelease(Registry::Task a_task)
		{
			F4SE::GetTaskInterface()->AddTask(std::move(a_task));
		}

		[[nodiscard]] static Owner AcquireOwner(RE::ReferenceEffectController* a_controller)
		{
			if (!a_controller ||
				*reinterpret_cast<uintptr_t*>(a_controller) != s_activeEffectControllerVtable)
				return {};

			auto* controller =
				static_cast<RE::ActiveEffectReferenceEffectController*>(a_controller);
			auto* owner = controller->effect;
			if (!owner || std::addressof(owner->hitEffectController) != controller)
			{
				REX::WARN(
					"Shader Reference Effect Lifetime: controller owner does not match the verified embedded layout."sv);
				return {};
			}

			return Owner{ owner };
		}

		static RE::ShaderReferenceEffect* __fastcall HKConstructor(
			RE::ShaderReferenceEffect* a_effect,
			RE::ReferenceEffectController* a_controller)
		{
			auto* result = s_constructor(a_effect, a_controller);
			if (result)
			{
				auto owner = AcquireOwner(a_controller);
				if (owner)
					GetRegistry().Retain(result, std::move(owner), QueueRelease);
			}
			return result;
		}

		static void* __fastcall HKDeletingDestructor(
			RE::ShaderReferenceEffect* a_effect,
			uint32_t a_flags)
		{
			return GetRegistry().RetireAfter(
				a_effect,
				[a_effect, a_flags]() {
					return s_deletingDestructor(a_effect, a_flags);
				},
				QueueRelease);
		}

		[[nodiscard]] static bool ValidateDeletingDestructor(uintptr_t a_target)
		{
			if (RELEX::IsRuntimeOG())
			{
				return RELEX::Validate(a_target,
					{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
						0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xE8,
						0x6C, 0xD9, 0xFF, 0xFF, 0xF6, 0xC3, 0x01, 0x74 });
			}
			if (RELEX::IsRuntimeNG())
			{
				return RELEX::Validate(a_target,
					{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
						0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
						0x8D, 0x05, 0xF2, 0x1A, 0x83, 0x01, 0x8B, 0xF2 });
			}
			if (RELEX::IsRuntimeAE())
			{
				return RELEX::Validate(a_target,
					{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
						0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
						0x8D, 0x05, 0x92, 0x1F, 0x9B, 0x01, 0x8B, 0xF2 });
			}
			return false;
		}
	}

	ModuleShaderReferenceEffectLifetime::ModuleShaderReferenceEffectLifetime() :
		Module("Shader Reference Effect Lifetime", &bFixesShaderReferenceEffectLifetime)
	{}

	bool ModuleShaderReferenceEffectLifetime::DoInstall(
		[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace shaderReferenceEffectLifetimeDetail;

		const auto constructorTarget =
			REL::ID{ 1546646, 2226732, 2226732 }.address();
		const auto deletingDestructorTarget =
			REL::ID{ 509, 2226770, 2226770 }.address();

		const bool constructorValid = RELEX::Validate(constructorTarget,
			{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
				0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF2 });
		if (!constructorValid ||
			!ValidateDeletingDestructor(deletingDestructorTarget))
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: unsupported bytes or an existing hook conflict; patch not applied."sv);
			return false;
		}

		s_activeEffectControllerVtable =
			RE::VTABLE::ActiveEffectReferenceEffectController[0].address();
		if (!s_activeEffectControllerVtable)
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: ActiveEffect controller vtable did not resolve; patch not applied."sv);
			return false;
		}

		s_deletingDestructor = reinterpret_cast<DeletingDestructor>(
			RELEX::DetourJump(
				deletingDestructorTarget,
				reinterpret_cast<uintptr_t>(&HKDeletingDestructor)));
		if (!s_deletingDestructor)
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: deleting-destructor detour failed; patch not applied."sv);
			return false;
		}

		s_constructor = reinterpret_cast<Constructor>(
			RELEX::DetourJump(
				constructorTarget,
				reinterpret_cast<uintptr_t>(&HKConstructor)));
		if (!s_constructor)
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: constructor detour failed; retirement hook remains inert."sv);
			return false;
		}

		return true;
	}
}
