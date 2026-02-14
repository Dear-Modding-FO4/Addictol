#include <Modules\AdModuleLoadScreen.h>
#include <AdUtils.h>
#include <RE\B\BSGraphics.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLoadScreen{ "Patches"sv, "bLoadScreen"sv, true };
	static RE::BSGraphics::RendererData* g_RendererData{ nullptr };
	static void DrawUILoadScreen(uint32_t a_unk) noexcept;
	decltype(&DrawUILoadScreen) origDrawUI{ nullptr };

	static void DrawUILoadScreen(uint32_t a_unk) noexcept
	{
		// get the address of the back buffer
		REX::W32::ID3D11Texture2D* backBuffer{ nullptr };
		g_RendererData->renderWindow[0].swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, (void**)&backBuffer);
		if (!backBuffer)
			return;

		REX::W32::ID3D11RenderTargetView* backTarget{ nullptr };
		g_RendererData->device->CreateRenderTargetView(backBuffer, NULL, &backTarget);
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

	bool ModuleLoadScreen::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLoadScreen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			
			g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(2704527).address();
			RELEX::DetourJump(REL::ID(2249232).address(), (uintptr_t)&GetRandomLoadScreen);
			origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(2222551).address());
			RELEX::DetourCall(REL::ID(2249225).address() + 0x3CC, (uintptr_t)&DrawUILoadScreen);
		}
		else
		{
			// OG

			g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(235166).address();
			RELEX::DetourJump(REL::ID(316170).address(), (uintptr_t)&GetRandomLoadScreen);
			origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(386550).address());
			RELEX::DetourCall(REL::ID(135719).address() + 0x414, (uintptr_t)&DrawUILoadScreen);
		}

		return true;
	}

	bool ModuleLoadScreen::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLoadScreen::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}