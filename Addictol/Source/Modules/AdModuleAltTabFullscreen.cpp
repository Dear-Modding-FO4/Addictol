#include <Modules/AdModuleAltTabFullscreen.h>
#include <AdUtils.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAltTabFullscreen{ "Fixes"sv, "bAltTabFullscreen"sv, true };

	using TD3D11CreateDeviceAndSwapChain = HRESULT(WINAPI*)(
		IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
		const D3D_FEATURE_LEVEL*, UINT, UINT,
		const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
		ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

	static TD3D11CreateDeviceAndSwapChain OriginalCreate = nullptr;
	static IDXGISwapChain* g_swapChain = nullptr;
	static HWND g_hwnd = nullptr;
	static WNDPROC g_origWndProc = nullptr;

	static LRESULT CALLBACK Hook_WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp) noexcept
	{
		if (g_swapChain)
		{
			const bool focusLost =
				a_msg == WM_KILLFOCUS ||
				(a_msg == WM_ACTIVATEAPP && a_wp == FALSE) ||
				(a_msg == WM_ACTIVATE && LOWORD(a_wp) == WA_INACTIVE);

			if (focusLost)
			{
				BOOL isFullscreen = FALSE;
				if (SUCCEEDED(g_swapChain->GetFullscreenState(&isFullscreen, nullptr)) && isFullscreen)
					g_swapChain->SetFullscreenState(FALSE, nullptr);
			}
		}

		if (g_origWndProc)
			return CallWindowProcA(g_origWndProc, a_hwnd, a_msg, a_wp, a_lp);
		return DefWindowProcA(a_hwnd, a_msg, a_wp, a_lp);
	}

	static HRESULT WINAPI Hook_D3D11Create(
		IDXGIAdapter*            a_adapter,
		D3D_DRIVER_TYPE          a_driverType,
		HMODULE                  a_software,
		UINT                     a_flags,
		const D3D_FEATURE_LEVEL* a_featureLevels,
		UINT                     a_numFeatureLevels,
		UINT                     a_sdkVersion,
		const DXGI_SWAP_CHAIN_DESC* a_desc,
		IDXGISwapChain**         a_outSwapChain,
		ID3D11Device**           a_outDevice,
		D3D_FEATURE_LEVEL*       a_outFeatureLevel,
		ID3D11DeviceContext**    a_outContext) noexcept
	{
		const auto hr = OriginalCreate
			? OriginalCreate(a_adapter, a_driverType, a_software, a_flags, a_featureLevels,
				a_numFeatureLevels, a_sdkVersion, a_desc, a_outSwapChain, a_outDevice,
				a_outFeatureLevel, a_outContext)
			: E_FAIL;

		if (FAILED(hr) || !a_outSwapChain || !*a_outSwapChain || !a_desc)
			return hr;

		// AddRef so WndProc dispatched during engine teardown won't dereference a freed swap chain.
		g_swapChain = *a_outSwapChain;
		g_swapChain->AddRef();
		g_hwnd = a_desc->OutputWindow;

		IDXGIFactory* factory = nullptr;
		if (SUCCEEDED(g_swapChain->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) && factory)
		{
			factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
			factory->Release();
		}

		if (g_hwnd && !g_origWndProc)
		{
			if (auto* prior = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC)))
			{
				g_origWndProc = prior;
				SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Hook_WndProc));
			}
		}

		return hr;
	}

	ModuleAltTabFullscreen::ModuleAltTabFullscreen() :
		Module("Alt-Tab Fullscreen", &bFixesAltTabFullscreen)
	{}

	bool ModuleAltTabFullscreen::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAltTabFullscreen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		OriginalCreate = reinterpret_cast<TD3D11CreateDeviceAndSwapChain>(RELEX::DetourIAT(
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&Hook_D3D11Create)));

		if (!OriginalCreate)
		{
			REX::WARN("Alt-Tab Fullscreen: D3D11CreateDeviceAndSwapChain not found in IAT."sv);
			return false;
		}

		return true;
	}

	bool ModuleAltTabFullscreen::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAltTabFullscreen::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
