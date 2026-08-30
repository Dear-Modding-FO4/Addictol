#include <Modules/AdModuleAltTabFullscreen.h>
#include <Core/AdUtils.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace Addictol
{

	using TD3D11CreateDeviceAndSwapChain = HRESULT(WINAPI *)(
		IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
		const D3D_FEATURE_LEVEL *, UINT, UINT,
		const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
		ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

	static std::atomic<TD3D11CreateDeviceAndSwapChain> OriginalCreate{ nullptr };
	static std::atomic<bool> OriginalCreateInstalling{ false };

	static HRESULT WINAPI Hook_D3D11Create(
		IDXGIAdapter *a_adapter,
		D3D_DRIVER_TYPE a_driverType,
		HMODULE a_software,
		UINT a_flags,
		const D3D_FEATURE_LEVEL *a_featureLevels,
		UINT a_numFeatureLevels,
		UINT a_sdkVersion,
		const DXGI_SWAP_CHAIN_DESC *a_desc,
		IDXGISwapChain **a_outSwapChain,
		ID3D11Device **a_outDevice,
		D3D_FEATURE_LEVEL *a_outFeatureLevel,
		ID3D11DeviceContext **a_outContext) noexcept
	{
		// Convert an exclusive-fullscreen swap chain to windowed
		DXGI_SWAP_CHAIN_DESC patchedDesc{};
		const DXGI_SWAP_CHAIN_DESC *descToUse = a_desc;
		if (a_desc && !a_desc->Windowed)
		{
			patchedDesc = *a_desc;
			patchedDesc.Windowed = TRUE;
			patchedDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			descToUse = &patchedDesc;
			REX::INFO("Alt-Tab Fullscreen: forcing exclusive swap chain to windowed ({}x{}, hwnd {})"sv,
					  a_desc->BufferDesc.Width, a_desc->BufferDesc.Height,
					  reinterpret_cast<void *>(a_desc->OutputWindow));
		}

		auto original = OriginalCreate.load(std::memory_order_acquire);
		while (!original && OriginalCreateInstalling.load(std::memory_order_acquire))
		{
			SwitchToThread();
			original = OriginalCreate.load(std::memory_order_acquire);
		}
		return original ?
			original(a_adapter, a_driverType, a_software, a_flags, a_featureLevels,
				a_numFeatureLevels, a_sdkVersion, descToUse, a_outSwapChain, a_outDevice,
				a_outFeatureLevel, a_outContext) :
			E_FAIL;
	}

	ModuleAltTabFullscreen::ModuleAltTabFullscreen() : Module("Alt-Tab Fullscreen", &bFixesAltTabFullscreen)
	{
	}

	bool ModuleAltTabFullscreen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		OriginalCreateInstalling.store(true, std::memory_order_release);
		const auto original = reinterpret_cast<TD3D11CreateDeviceAndSwapChain>(RELEX::DetourIAT(
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&Hook_D3D11Create)));

		if (!original)
		{
			OriginalCreateInstalling.store(false, std::memory_order_release);
			REX::WARN("Alt-Tab Fullscreen: D3D11CreateDeviceAndSwapChain not found in IAT."sv);
			return false;
		}

		OriginalCreate.store(original, std::memory_order_release);
		OriginalCreateInstalling.store(false, std::memory_order_release);
		return true;
	}

}
