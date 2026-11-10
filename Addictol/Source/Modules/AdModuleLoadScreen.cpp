#include <Modules/AdModuleLoadScreen.h>
#include <Core/AdUtils.h>
#include <RE/B/BSGraphics.h>

namespace Addictol
{
	static void DrawUILoadScreen(uint32_t a_unk) noexcept;
	decltype(&DrawUILoadScreen) origDrawUI{ nullptr };

	static void DrawUILoadScreen(uint32_t a_unk) noexcept
	{
		// fix black screen for ultra wide monitors
		const auto rendererData = RE::BSGraphics::GetRendererData();
		if (const auto backTarget = rendererData->renderWindow[0].swapChainRenderTarget.rtView)
		{
			static constexpr float color[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			rendererData->context->ClearRenderTargetView(backTarget, color);
		}
		origDrawUI(a_unk);
	}

	static void GetRandomLoadScreen() noexcept
	{
		return;
	}

	ModuleLoadScreen::ModuleLoadScreen() :
		Module("Load Screen", &bPatchesLoadScreen)
	{}

	bool ModuleLoadScreen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg)
		{
			if (!REX::W32::GetModuleHandleA("HighFPSPhysicsFix.dll"))
			{
				Skip("High FPS Physics Fix not installed; see https://www.nexusmods.com/fallout4/mods/44798"sv);
				return false;
			}

			if (!RELEX::IsRuntimeOG())
			{
				//	 We don't override the user's HFPF config.
				//   DisableAnimationOnLoadingScreens=true - HFPF NOPs 5 bytes at ServingThread::ThreadProc+0x223
				//   DisableBlackLoadingScreens=true - HFPF flips byte at LoadingMenu::ProcessMessage+0x116 (0x75 -> 0xEB)
				static constexpr std::uint8_t hfpfAnimNop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
				const auto* const animSite = reinterpret_cast<const std::uint8_t*>(REL::Relocation{ RE::ID::JobListManager::ServingThread::ThreadProc, REL::Offset(0x223) }.address());
				const auto* const blackSite = reinterpret_cast<const std::uint8_t*>(REL::Relocation{ RE::ID::LoadingMenu::ProcessMessage, REL::Offset(0x116) }.address());

				const bool hfpfAnimDisabled = std::memcmp(animSite, hfpfAnimNop, sizeof(hfpfAnimNop)) == 0;
				const bool hfpfBlackDisabled = *blackSite == 0xEB;

				const bool installAnimNoOp = hfpfAnimDisabled;
				const bool installDrawUIClear = !hfpfBlackDisabled;

				if (!installAnimNoOp && !installDrawUIClear)
				{
					Skip("HFPF already covers both jobs (DisableAnimationOnLoadingScreens=false, DisableBlackLoadingScreens=true)"sv);
					return false;
				}

				if (!hfpfAnimDisabled)
					REX::INFO("HighFPSPhysicsFix has DisableAnimationOnLoadingScreens=false; keeping loading-screen animations."sv);
				if (hfpfBlackDisabled)
					REX::INFO("HighFPSPhysicsFix has DisableBlackLoadingScreens=true; skipping ultra-wide black-background fix."sv);

				if (installAnimNoOp)
				{
					RELEX::DetourJump(RE::ID::LoadingMenu::PopulateLoadScreens.address(), (uintptr_t)&GetRandomLoadScreen);
				}
				if (installDrawUIClear)
				{
					origDrawUI = (decltype(&DrawUILoadScreen))(RE::ID::Interface3D::Renderer::RenderPostAA.address());
					RELEX::DetourCall(RE::ID::LoadingMenu::Render.address() + 0x3CC, (uintptr_t)&DrawUILoadScreen);
				}
			}
			else
			{
				// OG

				RELEX::WriteSafe(REL::Relocation{ RE::ID::LoadingMenu::PopulateLoadScreens, REL::Offset(0x1B) }.get(), { 0xE9, 0x34, 0x02, 0x00, 0x00, 0x90 });
				origDrawUI = (decltype(&DrawUILoadScreen))(RE::ID::Interface3D::Renderer::RenderPostAA.address());
				RELEX::DetourCall(RE::ID::LoadingMenu::Render.address() + 0x414, (uintptr_t)&DrawUILoadScreen);
			}
		}

		return true;
	}
}
