#include <Modules\AdModuleLoadScreen.h>
#include <AdUtils.h>
#include <REL\REL.h>
#include <REX\REX.h>
#include <RE\B\BSGraphics.h>
#include <RE\L\LoadingMenu.h>
//#include <d3d11.h>
//#include <dxgi.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesLoadScreen{ "Patches", "bLoadScreen", true };
	static RE::BSGraphics::RendererData* g_RendererData{ nullptr };

	//	// get the address of the front buffer
	//	REX::W32::ID3D11Texture2D* frontBuffer{ nullptr };
	//	g_RendererData->renderWindow[0].swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, (void**)&frontBuffer);	
	//	if (!frontBuffer)
	//		return;

	//	REX::W32::D3D11_TEXTURE2D_DESC descBuffer{};
	//	frontBuffer->GetDesc(&descBuffer);

	//	REX::INFO("lm {} {}", descBuffer.width, descBuffer.height);
	//
	//	//REX::W32::ID3D11RenderTargetView* frontTarget{ nullptr };
	//	//g_RendererData->device->CreateRenderTargetView(frontBuffer, NULL, &frontTarget);	
	//	//if (!frontTarget)
	//	//	return;

	//	//// Clear all (fixed ultra wide monitors)
	//	//static const float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	//	//g_RendererData->context->ClearRenderTargetView(frontTarget, color);

	//	//frontTarget->Release();

	static void GetRandomLoadScreen() noexcept
	{
		return;
	}

	ModuleLoadScreen::ModuleLoadScreen() :
		Module("Module Load Screen", &bPathesLoadScreen)
	{}

	bool ModuleLoadScreen::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLoadScreen::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			
			g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(2704527).address();
			RELEX::DetourJump(REL::ID(2249232).address(), (uintptr_t)&GetRandomLoadScreen);
		}
		else
		{
			// OG

			g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(235166).address();
			RELEX::DetourJump(REL::ID(316170).address(), (uintptr_t)&GetRandomLoadScreen);
		}

		return true;
	}
}