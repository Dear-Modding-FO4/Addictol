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

	using TSetFullscreenState = HRESULT(WINAPI*)(IDXGISwapChain*, BOOL, IDXGIOutput*);

	static TD3D11CreateDeviceAndSwapChain OriginalCreate = nullptr;
	static std::atomic<TSetFullscreenState> OriginalSetFullscreenState{ nullptr };
	static std::atomic<HWND> g_gameHwnd{ nullptr };
	static std::atomic<bool> g_vtablePatched{ false };

	static HRESULT WINAPI Hook_SetFullscreenState(IDXGISwapChain* a_this, BOOL a_fullscreen, IDXGIOutput* a_output) noexcept
	{
		const auto orig = OriginalSetFullscreenState.load(std::memory_order_acquire);

		// Only neuter our own swap chain; ReShade/ENB/overlay swap chains get forwarded.
		if (a_fullscreen)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			const auto descHr = a_this->GetDesc(&desc);
			const auto gameHwnd = g_gameHwnd.load(std::memory_order_relaxed);
			if (SUCCEEDED(descHr) && desc.OutputWindow == gameHwnd && gameHwnd)
			{
				REX::INFO("Alt-Tab Fullscreen: SetFullscreenState(TRUE) blocked on game window."sv);
				return S_OK;
			}
		}

		return orig ? orig(a_this, a_fullscreen, a_output) : S_OK;
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
		// Force borderless-windowed; staying out of exclusive avoids the alt-tab ResizeBuffers re-entry hang.
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
			const auto mwaHr = factory->MakeWindowAssociation(descToUse->OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
			factory->Release();
			if (FAILED(mwaHr))
			{
				REX::WARN("Alt-Tab Fullscreen: MakeWindowAssociation failed (hr=0x{:08X})."sv, static_cast<std::uint32_t>(mwaHr));
			}
		}

		// MakeWindowAssociation isn't enough; explicit SetFullscreenState(TRUE) bypasses it.
		g_gameHwnd.store(descToUse->OutputWindow, std::memory_order_relaxed);

		bool expected = false;
		if (g_vtablePatched.compare_exchange_strong(expected, true))
		{
			constexpr std::uint32_t kSetFullscreenStateSlot = 10;
			auto** const vtablePtr = *reinterpret_cast<void***>(*a_outSwapChain);
			// Publish original before the swap so the forward path never sees nullptr.
			OriginalSetFullscreenState.store(
				reinterpret_cast<TSetFullscreenState>(vtablePtr[kSetFullscreenStateSlot]),
				std::memory_order_release);

			const auto vtableAddr = reinterpret_cast<std::uintptr_t>(vtablePtr);
			const auto returned = RELEX::DetourVTable(
				vtableAddr,
				reinterpret_cast<std::uintptr_t>(&Hook_SetFullscreenState),
				kSetFullscreenStateSlot);
			if (!returned)
			{
				REX::WARN("Alt-Tab Fullscreen: SetFullscreenState vtable patch failed."sv);
				OriginalSetFullscreenState.store(nullptr, std::memory_order_release);
				g_vtablePatched.store(false);
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
