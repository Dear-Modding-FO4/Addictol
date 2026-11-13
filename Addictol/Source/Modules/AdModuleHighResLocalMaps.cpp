// Thanks doodlum for the idea & original mod: https://github.com/doodlum/skyrim-hd-local-map

#include <Modules/AdModuleHighResLocalMaps.h>
#include <Core/AdUtils.h>

#include <RE/B/BSGraphics.h>

namespace Addictol
{
	namespace highResLocalMapsDetail
	{
		inline thread_local bool CurrentMapRendererIsCompanion = false;
		inline uint32_t MainWidth = 1280;
		inline uint32_t MainHeight = 720;

		struct CreateRenderTarget // BSGraphics::RenderTargetManager::CreateRenderTarget
		{
			static void thunk_capture(void* a_a1, uint32_t a_target, RE::BSGraphics::RenderTargetProperties* a_properties, void* a_a4)
			{
				REX::INFO("HighResLocalMaps: Capturing Render Target {}'s Resolution: {}x{}."sv, a_target, a_properties->width, a_properties->height);
				MainWidth = a_properties->width;
				MainHeight = a_properties->height;

				return thunk_scale(a_a1, a_target, a_properties, a_a4);
			}

			static void thunk_scale(void* a_a1, uint32_t a_target, RE::BSGraphics::RenderTargetProperties* a_properties, void* a_a4)
			{
				auto scaledWidth = static_cast<uint32_t>(ceil((float)MainWidth * fAdditionalLocalMapScaleFactor.GetValue()));
				auto scaledHeight = static_cast<uint32_t>(ceil((float)MainHeight * fAdditionalLocalMapScaleFactor.GetValue()));

				REX::INFO("HighResLocalMaps: Updating Render Target {} from {}x{} to {}x{}."sv, a_target, a_properties->width, a_properties->height,
					scaledWidth, scaledHeight);
				a_properties->width = scaledWidth;
				a_properties->height = scaledHeight;

				return func(a_a1, a_target, a_properties, a_a4);
			}

			static inline REL::Relocation<decltype(thunk_scale)> func;
		};

		struct CreateDepthStencilTarget // BSGraphics::RenderTargetManager::CreateDepthStencilTarget()
		{
			static void thunk(void* a_a1, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties, void* a_a4)
			{
				auto scaledWidth = static_cast<uint32_t>(ceil((float)MainWidth * fAdditionalLocalMapScaleFactor.GetValue()));
				auto scaledHeight = static_cast<uint32_t>(ceil((float)MainHeight * fAdditionalLocalMapScaleFactor.GetValue()));

				REX::INFO("HighResLocalMaps: Updating Depth Stencil Target {} from {}x{} to {}x{}."sv, a_target, a_properties->width,
					a_properties->height, scaledWidth, scaledHeight);
				a_properties->width = scaledWidth;
				a_properties->height = scaledHeight;

				return func(a_a1, a_target, a_properties, a_a4);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Render // LocalMapRenderer::Render
		{
			static bool thunk(void* a_a1, void* a_a2, void* a_a3)
			{
				auto& outputRenderTarget = *reinterpret_cast<int32_t*>(static_cast<std::byte*>(a_a1) + 0x2A0);
				CurrentMapRendererIsCompanion = outputRenderTarget == 20;
				outputRenderTarget = 20;

				return func(a_a1, a_a2, a_a3);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RenderEffect // ImageSpaceManager::RenderEffect
		{
			static uintptr_t thunk(void* a_a1, [[maybe_unused]] uintptr_t a_effectEnum, void* a_a3,
				[[maybe_unused]] int32_t a_outputTarget, void* a_a5)
			{
				if (!CurrentMapRendererIsCompanion)
					// Local Map Output
					return func(a_a1, RELEX::IsRuntimeOG() ? 152 : 153, a_a3, 19, a_a5);
				else
				 	// Companion Map Output
					return func(a_a1, RELEX::IsRuntimeOG() ? 153 : 154, a_a3, 20, a_a5);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleHighResLocalMaps::ModuleHighResLocalMaps() :
		Module("High Res Local Maps", &bPatchesHighResLocalMaps)
	{}

	bool ModuleHighResLocalMaps::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Targets
		const auto targetCreate 					= RE::ID::BSShaderRenderTargets::Create.address();
		const auto targetLocalMapOutput 			= targetCreate + REL::Offset{ 0x62D, 0x650 }.offset();
		const auto targetCompanionMapOutput 		= targetCreate + REL::Offset{ 0x66B, 0x690 }.offset();
		const auto targetCompanionMapPrimary 		= targetCreate + REL::Offset{ 0x717, 0x708 }.offset();
		const auto targetCompanionMapSecondary 		= targetCreate + REL::Offset{ 0x6A5, 0x6CC }.offset();
		const auto targetCompanionMapDepthStencil 	= targetCreate + REL::Offset{ 0x749, 0x73E }.offset();
		const auto targetRender 					= RE::ID::LocalMapRenderer::Render.address();
		const auto targetRenderEffect 				= targetRender + REL::Offset{ 0x97E, 0xA3B }.offset();

		const auto createRenderTarget				= RE::ID::BSGraphics::RenderTargetManager::CreateRenderTarget.address();
		const auto createDepthStencilTarget			= RE::ID::BSGraphics::RenderTargetManager::CreateDepthStencilTarget.address();

		const REL::Relocation<uint32_t*> targetCompanionMapCellCap{ REL::ID{ 945604 } };

		// Validate
		if (!RELEX::IsCallTo(targetLocalMapOutput, 				createRenderTarget) 		||
			!RELEX::IsCallTo(targetCompanionMapOutput, 			createRenderTarget) 		||
			!RELEX::IsCallTo(targetCompanionMapPrimary, 		createRenderTarget) 		||
			!RELEX::IsCallTo(targetCompanionMapSecondary, 		createRenderTarget) 		||
			!RELEX::IsCallTo(targetCompanionMapDepthStencil, 	createDepthStencilTarget) 	||
			!RELEX::Validate(targetRenderEffect, { 0xE8 }) 									||
			!RELEX::Validate(targetRender, { 0x48, 0x8B, 0xC4, 0x88, 0x50, 0x10 })			||
			*targetCompanionMapCellCap != 5)
			return false;
		
		// Lift the Companion Map Cell Cap
		*targetCompanionMapCellCap = 0xFFFF;
		
		// Upsample the Render and Depth Stencil Targets
		// Originals first, so no installed thunk can run without one
		highResLocalMapsDetail::CreateRenderTarget::func = createRenderTarget;
		highResLocalMapsDetail::CreateDepthStencilTarget::func = createDepthStencilTarget;
		bool installed = RELEX::DetourClassCall(targetLocalMapOutput, &highResLocalMapsDetail::CreateRenderTarget::thunk_capture) != 0;		// 19 Local Map Output
		installed &= RELEX::DetourClassCall(targetCompanionMapOutput, &highResLocalMapsDetail::CreateRenderTarget::thunk_scale) != 0;		// 20 Companion Map Output
		installed &= RELEX::DetourClassCall(targetCompanionMapPrimary, &highResLocalMapsDetail::CreateRenderTarget::thunk_scale) != 0;		// 23 Companion Map Primary
		installed &= RELEX::DetourClassCall(targetCompanionMapSecondary, &highResLocalMapsDetail::CreateRenderTarget::thunk_scale) != 0;	// 21 Companion Map Secondary
		installed &= RELEX::DetourClassCall(targetCompanionMapDepthStencil, &highResLocalMapsDetail::CreateDepthStencilTarget::thunk) != 0;	// 11 Companion Map Depth Stencil

		// Use the Companion app's Render Targets
		highResLocalMapsDetail::Render::func = RELEX::DetourClassJump(targetRender, &highResLocalMapsDetail::Render::thunk);
		highResLocalMapsDetail::RenderEffect::func = RELEX::DetourClassCall(targetRenderEffect, &highResLocalMapsDetail::RenderEffect::thunk);

		// Validate the Funcs
		return 	installed &&
				highResLocalMapsDetail::Render::func != 0 &&
				highResLocalMapsDetail::RenderEffect::func != 0;
	}

	bool ModuleHighResLocalMaps::HasProcessDefender() noexcept
	{
		return true;
	}
}
