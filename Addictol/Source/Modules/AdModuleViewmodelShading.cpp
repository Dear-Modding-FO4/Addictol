#include <Modules/AdModuleViewmodelShading.h>
#include <AdUtils.h>

#include <RE/B/BSShaderAccumulator.h>
#include <RE/B/BSShaderManager.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesViewmodelShading{ "Fixes"sv, "bViewmodelShading"sv, true };

	using TMove1stPersonToOrigin = void(__fastcall*)();
	static TMove1stPersonToOrigin OriginalMove1stPersonToOrigin = nullptr;

	// State* @ +0x190 = NiPoint3 forwardLightOffset (see BSShaderManager.h).
	// Accumulator* @ +0x570 = NiPoint3A eyePosition (see BSShaderAccumulator.h).
	static constexpr std::size_t kForwardLightOffset = 0x190;
	static constexpr std::size_t kEyePosition = 0x570;

	// Singletons resolved at install time; both are pointer-to-pointer globals.
	static RE::BSShaderManager::State** g_stateGlobal = nullptr;
	static RE::BSShaderAccumulator** g_accumulatorGlobal = nullptr;

	// Add forwardLightOffset.xyz onto eyePosition.xyz; the original write at
	// +0x570/+0x574/+0x578 omitted this term, breaking 1st-person specular.
	static void __fastcall HookMove1stPersonToOrigin() noexcept
	{
		if (OriginalMove1stPersonToOrigin)
			OriginalMove1stPersonToOrigin();

		auto* state = g_stateGlobal ? *g_stateGlobal : nullptr;
		auto* accumulator = g_accumulatorGlobal ? *g_accumulatorGlobal : nullptr;
		if (!state || !accumulator)
			return;

		auto* statePtr = reinterpret_cast<std::byte*>(state);
		auto* accumulatorPtr = reinterpret_cast<std::byte*>(accumulator);

		const auto& offset = *reinterpret_cast<const RE::NiPoint3*>(statePtr + kForwardLightOffset);
		auto* eye = reinterpret_cast<float*>(accumulatorPtr + kEyePosition);

		eye[0] += offset.x;
		eye[1] += offset.y;
		eye[2] += offset.z;
	}

	ModuleViewmodelShading::ModuleViewmodelShading() :
		Module("Viewmodel Shading", &bFixesViewmodelShading)
	{}

	bool ModuleViewmodelShading::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleViewmodelShading::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		g_stateGlobal = reinterpret_cast<RE::BSShaderManager::State**>(REL::ID{ 1444212, 2712877 }.address());
		g_accumulatorGlobal = reinterpret_cast<RE::BSShaderAccumulator**>(REL::ID{ 1430301, 2712932 }.address());

		const auto target = REL::ID{ 76526, 2318293 }.address();
		*reinterpret_cast<uintptr_t*>(&OriginalMove1stPersonToOrigin) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&HookMove1stPersonToOrigin));
		return OriginalMove1stPersonToOrigin != nullptr;
	}

	bool ModuleViewmodelShading::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleViewmodelShading::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
