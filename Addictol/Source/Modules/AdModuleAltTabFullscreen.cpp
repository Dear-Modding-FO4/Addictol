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
		// Force borderless-windowed when the engine requested exclusive fullscreen.
		// The vanilla alt-tab hang is rooted in exclusive-mode ResizeBuffers re-entry;
		// never entering exclusive eliminates the bug class entirely.
		DXGI_SWAP_CHAIN_DESC patchedDesc{};
		const DXGI_SWAP_CHAIN_DESC* descToUse = a_desc;
		if (a_desc && !a_desc->Windowed)
		{
			patchedDesc = *a_desc;
			patchedDesc.Windowed = TRUE;
			patchedDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			descToUse = &patchedDesc;
			REX::INFO("Alt-Tab Fullscreen: forcing exclusive swap chain to windowed ({}x{}, hwnd {})"sv,
				a_desc->BufferDesc.Width, a_desc->BufferDesc.Height,
				reinterpret_cast<void*>(a_desc->OutputWindow));
		}

		const auto hr = OriginalCreate
			? OriginalCreate(a_adapter, a_driverType, a_software, a_flags, a_featureLevels,
				a_numFeatureLevels, a_sdkVersion, descToUse, a_outSwapChain, a_outDevice,
				a_outFeatureLevel, a_outContext)
			: E_FAIL;

		if (FAILED(hr) || !a_outSwapChain || !*a_outSwapChain || !descToUse)
			return hr;

		// Block DXGI's auto Alt+Enter; without this, DXGI can transition us back into exclusive.
		IDXGIFactory* factory = nullptr;
		if (SUCCEEDED((*a_outSwapChain)->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) && factory)
		{
			factory->MakeWindowAssociation(descToUse->OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
			factory->Release();
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
