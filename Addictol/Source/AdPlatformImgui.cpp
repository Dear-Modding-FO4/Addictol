#include <AdPlatformImgui.h>
#include <AdUtils.h>
#include <RE/B/BSGraphics.h>
#include <detours/Detours.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <Windows.h>

#undef ERROR

extern IMGUI_IMPL_API LRESULT WINAPI ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Addictol
{
	void PlatformImgui::RefreshCursor() noexcept
	{
		auto platform = PlatformImgui::GetSingleton();
		if (platform->initMain)
		{
			auto& io = ImGui::GetIO();

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(reinterpret_cast<HWND>(platform->context.hwnd), &pt);

			io.MousePos.x = static_cast<float>(pt.x);
			io.MousePos.y = static_cast<float>(pt.y);
		}
	}

	void PlatformImgui::KillWindow(uint32_t a_unk) noexcept
	{
		auto platform = PlatformImgui::GetSingleton();
		
		platform->KillHooks();
		platform->KillSDM();

		KillWindowOrig(a_unk);
	}

	void PlatformImgui::WindowSizeChanged(uint32_t a_unk) noexcept
	{
		WindowSizeChangedOrig(a_unk);

		auto platform = PlatformImgui::GetSingleton();
		if (platform->initMain)
			REX::W32::GetClientRect(platform->context.hwnd, &platform->context.windowRect);
	}

	void PlatformImgui::UIBeforeCursorEndFrame(void* a_UI) noexcept
	{
		auto platform = PlatformImgui::GetSingleton();
		if (platform->initMain)
		{
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
		//	RefreshCursor();

			for (auto& it : platform->DrawBeforeCursorHandlers)
				if (it.second)
					it.second();

	//		ImGui::ShowDemoWindow();

			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		UIBeforeCursorEndFrameOrig(a_UI);
	}

	void PlatformImgui::UIEndFrame() noexcept
	{
		UIEndFrameOrig();

		auto platform = PlatformImgui::GetSingleton();
		if (!platform->initMain) return;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

	//	RefreshCursor();

		for (auto& it : platform->DrawOverlappHandlers)
			if (it.second)
				it.second();

	//	ImGui::ShowDemoWindow();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	uint64_t PlatformImgui::WindowProc(REX::W32::HWND a_hwnd, uint32_t a_msg, 
		uint64_t a_wparam, uint64_t a_lparam) noexcept
	{
		if (ImGui_ImplWin32_WndProcHandler(reinterpret_cast<HWND>(a_hwnd), a_msg, a_wparam, a_lparam))
			return S_FALSE;

		return WindowProcOrig(a_hwnd, a_msg, a_wparam, a_lparam);
	}

	PlatformImgui::~PlatformImgui() noexcept
	{
		KillHooks();
		KillSDM();
	}

	bool PlatformImgui::InitHooks() noexcept
	{
		// Hook window procedure
		*(uintptr_t*)&WindowProcOrig =
			RELEX::DetourClassJump(REL::ID{ 1117598, 2228990 }.address(), &WindowProc);
		
		// Hook destroy window
		*(uintptr_t*)&KillWindowOrig =
			RELEX::DetourClassJump(REL::ID{ 1117598, 2276823 }.address(), &KillWindow);

		// Hook changed window size
		*(uintptr_t*)&WindowSizeChangedOrig =
			RELEX::DetourClassJump(REL::ID{ 212827, 2276824 }.address(), &WindowSizeChanged);

		// Hook UI ScreenSpace_RenderMenus()
		*(uintptr_t*)&UIBeforeCursorEndFrameOrig =
			RELEX::DetourClassJump(REL::ID{ 230711, 2284762 }.address(), &UIBeforeCursorEndFrame);

		// Hook UI EndFrame() 
		//*(uintptr_t*)&UIEndFrameOrig =
		//	RELEX::DetourClassJump(REL::ID{ 137303, 2284763 }.address(), &UIEndFrame);

		return KillWindowOrig && WindowSizeChangedOrig && UIBeforeCursorEndFrameOrig && UIEndFrameOrig;
	}

	bool PlatformImgui::InitSDM() noexcept
	{
		if (initMain) return true;

		auto rendererData = reinterpret_cast<RE::BSGraphics::RendererData*>(REL::ID{ 235166, 2704527 }.address());
		if (!rendererData)
		{
			REX::ERROR("RE::BSGraphics::RendererData non-exists"sv);
			return false;
		}

		context.hwnd = rendererData->renderWindow[0].hwnd;
		context.device = rendererData->device;
		context.deviceContext = rendererData->context;
		REX::W32::GetClientRect(context.hwnd, &context.windowRect);
		context.imguiContext = ImGui::CreateContext();
		if (!context.imguiContext)
		{
			REX::ERROR("ImGui::CreateContext() return failed"sv);
			return false;
		}

		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;		// Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;		// Enable Gamepad Controls

		io.Fonts->AddFontDefault();

		// Make process DPI aware and obtain main monitor scale
		ImGui_ImplWin32_EnableDpiAwareness();
		return initMain = ImGui_ImplWin32_Init(context.hwnd) &&
			ImGui_ImplDX11_Init(reinterpret_cast<ID3D11Device*>(context.device),
				reinterpret_cast<ID3D11DeviceContext*>(context.deviceContext));
	}

	void PlatformImgui::KillHooks() noexcept
	{
		if (initHooks)
		{
			initHooks = false;

			// Cleanup
			Detours::X64::DetourRemove(*(uintptr_t*)&WindowProcOrig);
			Detours::X64::DetourRemove(*(uintptr_t*)&UIBeforeCursorEndFrameOrig);
			Detours::X64::DetourRemove(*(uintptr_t*)&UIEndFrameOrig);
			Detours::X64::DetourRemove(*(uintptr_t*)&KillWindowOrig);
			Detours::X64::DetourRemove(*(uintptr_t*)&WindowSizeChangedOrig);
		}
	}

	void PlatformImgui::KillSDM() noexcept
	{
		if (initMain)
		{
			initMain = false;

			// Cleanup
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
	}
}