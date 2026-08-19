#include <Modules/AdModuleLoadScreen.h>
#include <AdUtils.h>
#include <RE/B/BSGraphics.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLoadScreen{ "Patches"sv, "bLoadScreen"sv, true };

	static RE::BSGraphics::RendererData* g_RendererData{ nullptr };
	static void DrawUILoadScreen(uint32_t a_unk) noexcept;
	decltype(&DrawUILoadScreen) origDrawUI{ nullptr };

	static void DrawUILoadScreen(uint32_t a_unk) noexcept
	{
		REX::W32::ID3D11Texture2D* backBuffer{ nullptr };
		g_RendererData->renderWindow[0].swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, (void**)&backBuffer);
		if (!backBuffer)
			return;

		REX::W32::ID3D11RenderTargetView* backTarget{ nullptr };
		g_RendererData->device->CreateRenderTargetView(backBuffer, NULL, &backTarget);
		backBuffer->Release();
		if (!backTarget)
			return;

		// fix black screen for ultra wide monitors
		static const float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		g_RendererData->context->ClearRenderTargetView(backTarget, color);

		backTarget->Release();
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
				//   DisableAnimationOnLoadingScreens=true - HFPF NOPs 5 bytes at REL::ID(2227631)+0x223
				//   DisableBlackLoadingScreens=true - HFPF flips byte at REL::ID(2249217)+0x116 (0x75 -> 0xEB)
				static constexpr std::uint8_t hfpfAnimNop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
				const auto* const animSite = reinterpret_cast<const std::uint8_t*>(REL::ID(2227631).address() + 0x223);
				const auto* const blackSite = reinterpret_cast<const std::uint8_t*>(REL::ID(2249217).address() + 0x116);

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
					RELEX::DetourJump(REL::ID(2249232).address(), (uintptr_t)&GetRandomLoadScreen);
				}
				if (installDrawUIClear)
				{
					g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(2704527).address();
					origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(2222551).address());
					RELEX::DetourCall(REL::ID(2249225).address() + 0x3CC, (uintptr_t)&DrawUILoadScreen);
				}
			}
			else
			{
				// OG

				g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(235166).address();
				RELEX::WriteSafe(REL::Relocation{ REL::ID(316170), REL::Offset(0x1B) }.get(), { 0xE9, 0x34, 0x02, 0x00, 0x00, 0x90 });
				origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(386550).address());
				RELEX::DetourCall(REL::ID(135719).address() + 0x414, (uintptr_t)&DrawUILoadScreen);
			}
		}

		return true;
	}

}
