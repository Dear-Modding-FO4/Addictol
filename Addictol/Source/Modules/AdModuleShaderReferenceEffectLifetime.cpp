#include <Modules/AdModuleShaderReferenceEffectLifetime.h>
#include <Core/AdDependentRegistry.h>
#include <Core/AdUtils.h>

#include <RE/A/ActiveEffect.h>
#include <RE/A/ActiveEffectReferenceEffectController.h>
#include <RE/R/ReferenceEffectController.h>
#include <RE/S/ShaderReferenceEffect.h>

namespace Addictol
{
	namespace shaderReferenceEffectLifetimeDetail
	{
		using Registry = DependentRegistry<RE::ActiveEffect*, RE::ShaderReferenceEffect*>;
		using Constructor = RE::ShaderReferenceEffect* (__fastcall*)(
			RE::ShaderReferenceEffect*, RE::ReferenceEffectController*);
		using ShaderDeletingDestructor = void* (__fastcall*)(RE::ShaderReferenceEffect*, uint32_t);
		using OwnerDestructor = void* (__fastcall*)(RE::ActiveEffect*);
		using OwnerDeletingDestructor = void* (__fastcall*)(RE::ActiveEffect*, uint32_t);

		static inline Constructor s_constructor{};
		static inline ShaderDeletingDestructor s_shaderDeletingDestructor{};
		static inline OwnerDestructor s_ownerDestructor{};
		static inline OwnerDeletingDestructor s_ownerDeletingDestructor{};
		static inline uintptr_t s_activeEffectControllerVtable{};

		[[nodiscard]] static Registry& GetRegistry()
		{
			// Leaked so hooks running during static teardown never touch a destroyed registry.
			static auto* registry = new Registry;
			return *registry;
		}

		[[nodiscard]] static RE::ActiveEffect* GetOwner(RE::ReferenceEffectController* a_controller)
		{
			if (!a_controller ||
				*reinterpret_cast<uintptr_t*>(a_controller) != s_activeEffectControllerVtable)
				return nullptr;

			auto* controller =
				static_cast<RE::ActiveEffectReferenceEffectController*>(a_controller);
			auto* owner = controller->effect;
			if (!owner || std::addressof(owner->hitEffectController) != controller)
			{
				REX::WARN(
					"Shader Reference Effect Lifetime: controller owner does not match the verified embedded layout."sv);
				return nullptr;
			}

			return owner;
		}

		// Vanilla StopHitEffects detach, which ~ActiveEffect skips once the target is cleared.
		static void DetachShaders(RE::ActiveEffect* a_owner)
		{
			GetRegistry().Release(a_owner, [a_owner](RE::ShaderReferenceEffect* a_effect) {
				if (a_effect->controller != std::addressof(a_owner->hitEffectController))
					return;

				a_effect->finished = true;
				a_effect->controller = nullptr;
			});
		}

		static RE::ShaderReferenceEffect* __fastcall HKConstructor(
			RE::ShaderReferenceEffect* a_effect,
			RE::ReferenceEffectController* a_controller)
		{
			auto* result = s_constructor(a_effect, a_controller);
			if (result)
			{
				if (auto* owner = GetOwner(a_controller))
					GetRegistry().Link(owner, result);
			}
			return result;
		}

		static void* __fastcall HKShaderDeletingDestructor(
			RE::ShaderReferenceEffect* a_effect,
			uint32_t a_flags)
		{
			GetRegistry().Unlink(a_effect);
			return s_shaderDeletingDestructor(a_effect, a_flags);
		}

		static void* __fastcall HKOwnerDestructor(RE::ActiveEffect* a_owner)
		{
			DetachShaders(a_owner);
			return s_ownerDestructor(a_owner);
		}

		static void* __fastcall HKOwnerDeletingDestructor(RE::ActiveEffect* a_owner, uint32_t a_flags)
		{
			DetachShaders(a_owner);
			return s_ownerDeletingDestructor(a_owner, a_flags);
		}

		[[nodiscard]] static bool ValidateShaderDeletingDestructor(uintptr_t a_target)
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

		[[nodiscard]] static bool ValidateOwnerDestructor(uintptr_t a_target)
		{
			// mov [rsp+10h], rbx; push rdi; sub rsp, 20h; cmp qword ptr [rcx+58h], 0 (target); lea rax, vftable
			return RELEX::Validate(a_target,
				{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
					0xEC, 0x20, 0x48, 0x83, 0x79, 0x58, 0x00, 0x48,
					0x8D, 0x05 });
		}

		[[nodiscard]] static bool ValidateOwnerDeletingDestructor(uintptr_t a_target)
		{
			// NG/AE inline the ~ActiveEffect body, including its target-gated StopHitEffects call.
			return RELEX::Validate(a_target,
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
					0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
					0x83, 0x79, 0x58, 0x00, 0x48, 0x8D, 0x05 });
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
			REL::ID{ 1546646, 2226732 }.address();
		const auto shaderDeletingDestructorTarget =
			REL::ID{ 509, 2226770 }.address();
		const auto ownerDestructorTarget =
			REL::ID{ 195712, 2225990 }.address();
		// OG's base deleting destructor calls ~ActiveEffect instead of inlining it.
		const auto ownerDeletingDestructorTarget =
			RELEX::IsRuntimeOG() ? 0 : REL::ID{ 2226042 }.address();

		const bool constructorValid = RELEX::Validate(constructorTarget,
			{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
				0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF2 });
		if (!constructorValid ||
			!ValidateShaderDeletingDestructor(shaderDeletingDestructorTarget) ||
			!ValidateOwnerDestructor(ownerDestructorTarget) ||
			(ownerDeletingDestructorTarget && !ValidateOwnerDeletingDestructor(ownerDeletingDestructorTarget)))
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

		// Unlink and detach hooks go in before Link so no linked shader can outlive its tracking.
		s_shaderDeletingDestructor = reinterpret_cast<ShaderDeletingDestructor>(
			RELEX::DetourJump(
				shaderDeletingDestructorTarget,
				reinterpret_cast<uintptr_t>(&HKShaderDeletingDestructor)));
		s_ownerDestructor = reinterpret_cast<OwnerDestructor>(
			RELEX::DetourJump(
				ownerDestructorTarget,
				reinterpret_cast<uintptr_t>(&HKOwnerDestructor)));
		if (ownerDeletingDestructorTarget)
		{
			s_ownerDeletingDestructor = reinterpret_cast<OwnerDeletingDestructor>(
				RELEX::DetourJump(
					ownerDeletingDestructorTarget,
					reinterpret_cast<uintptr_t>(&HKOwnerDeletingDestructor)));
		}
		if (!s_shaderDeletingDestructor || !s_ownerDestructor ||
			(ownerDeletingDestructorTarget && !s_ownerDeletingDestructor))
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: destructor detour failed; constructor not hooked, so no shader is tracked."sv);
			return false;
		}

		s_constructor = reinterpret_cast<Constructor>(
			RELEX::DetourJump(
				constructorTarget,
				reinterpret_cast<uintptr_t>(&HKConstructor)));
		if (!s_constructor)
		{
			REX::WARN(
				"Shader Reference Effect Lifetime: constructor detour failed; destructor hooks remain inert."sv);
			return false;
		}

		return true;
	}
}
