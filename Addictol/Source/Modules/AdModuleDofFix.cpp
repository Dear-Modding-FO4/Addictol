#include <Modules/AdModuleDofFix.h>
#include <AdUtils.h>

#include <RE/B/BSShaderAccumulator.h>
#include <RE/I/ImageSpaceEffectDepthOfField.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesDofFix{ "Fixes"sv, "bDofFix"sv, true };

	// Worker takes 5 args (5th at [rsp+0x28]); forward all five or the original loads garbage.
	// OG arg2 is the effect pointer; NG/AE arg2 is an index into effectList._data (+0x18).
	using TRenderEffect = void(__fastcall*)(void*, std::uintptr_t, std::int32_t, std::int32_t, void*);
	static TRenderEffect OriginalRenderEffect = nullptr;

	using TRenderScene = void(__fastcall*)(void*, void*, bool);
	static TRenderScene OriginalRenderScene = nullptr;

	static std::uintptr_t g_dofVTable = 0;
	static void** g_viewmodelCameraGlobal = nullptr;
	static void** g_viewmodelAccumGlobal = nullptr;
	static bool g_workerArgIsEffectPtr = false;

	static constexpr std::size_t kEffectListData = 0x18;
	static constexpr std::size_t kFirstPersonFlag = 0xB0;

	static void __fastcall HookRenderEffect(void* a_manager, std::uintptr_t a_idxOrEffect, std::int32_t a_a3, std::int32_t a_a4, void* a_a5) noexcept
	{
		void* effect = nullptr;
		if (g_workerArgIsEffectPtr)
		{
			effect = reinterpret_cast<void*>(a_idxOrEffect);
		}
		else if (a_manager)
		{
			void** data = *reinterpret_cast<void** const*>(reinterpret_cast<const std::byte*>(a_manager) + kEffectListData);
			if (data)
				effect = data[static_cast<std::uint32_t>(a_idxOrEffect)];
		}

		const bool isDof = effect && g_dofVTable && *reinterpret_cast<const std::uintptr_t*>(effect) == g_dofVTable;

		if (OriginalRenderEffect)
			OriginalRenderEffect(a_manager, a_idxOrEffect, a_a3, a_a4, a_a5);

		if (!isDof || !OriginalRenderScene || !g_viewmodelCameraGlobal || !g_viewmodelAccumGlobal)
			return;

		auto* camera = *g_viewmodelCameraGlobal;
		auto* accumulator = *g_viewmodelAccumGlobal;
		if (!camera || !accumulator)
			return;

		const auto firstPerson = *reinterpret_cast<const bool*>(reinterpret_cast<const std::byte*>(accumulator) + kFirstPersonFlag);
		if (!firstPerson)
			return;

		OriginalRenderScene(camera, accumulator, false);
	}

	ModuleDofFix::ModuleDofFix() :
		Module("DoF Fix", &bFixesDofFix)
	{}

	bool ModuleDofFix::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleDofFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		g_dofVTable = RE::VTABLE::ImageSpaceEffectDepthOfField[0].address();
		if (!g_dofVTable)
		{
			REX::WARN("[DoFFix] Could not resolve ImageSpaceEffectDepthOfField vtable; skipping."sv);
			return false;
		}

		g_workerArgIsEffectPtr = RELEX::IsRuntimeOG();

		// Reuse the engine's camera + accumulator from BSShaderUtil::RenderScene for pipeline parity.
		g_viewmodelCameraGlobal = reinterpret_cast<void**>(REL::ID{ 300623, 2712879 }.address());
		g_viewmodelAccumGlobal = reinterpret_cast<void**>(REL::ID{ 726120, 2712936 }.address());

		const auto renderSceneTarget = REL::ID{ 1310228, 2317576 }.address();
		OriginalRenderScene = reinterpret_cast<TRenderScene>(renderSceneTarget);

		const auto renderEffectTarget = REL::ID{ 325252, 2316595 }.address();
		*reinterpret_cast<std::uintptr_t*>(&OriginalRenderEffect) =
			RELEX::DetourJump(renderEffectTarget, reinterpret_cast<std::uintptr_t>(&HookRenderEffect));
		return OriginalRenderEffect != nullptr;
	}

	bool ModuleDofFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleDofFix::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
