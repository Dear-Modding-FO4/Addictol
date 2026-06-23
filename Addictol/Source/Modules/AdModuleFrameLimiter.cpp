#include <Modules/AdModuleFrameLimiter.h>
#include <AdUtils.h>

#include <RE/U/UI.h>
#include <RE/L/LoadingMenu.h>

#include <Windows.h>
#include <intrin.h>
#include <d3d11.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace Addictol
{
	static REX::TOML::Bool<> bLimiterFrameLimiter{ "Limiter"sv, "bFrameLimiter"sv, false };
	static REX::TOML::F32<> fFrameLimit{ "Limiter"sv, "fFrameLimit"sv, 0.0f };
	static REX::TOML::I32<> nVSync{ "Limiter"sv, "nVSync"sv, 0 };
	static REX::TOML::I32<> nVSyncInterval{ "Limiter"sv, "nVSyncInterval"sv, 1 };
	static REX::TOML::Bool<> bAllowTearing{ "Limiter"sv, "bAllowTearing"sv, true };
	static REX::TOML::Bool<> bForceFlipModel{ "Limiter"sv, "bForceFlipModel"sv, false };
	static REX::TOML::F32<> fLoadingFrameLimit{ "Limiter"sv, "fLoadingFrameLimit"sv, 0.0f };
	static REX::TOML::Bool<> bDisableVSyncWhileLoading{ "Limiter"sv, "bDisableVSyncWhileLoading"sv, true };

	namespace frameLimiterDetail
	{
		using TD3D11Create = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
			const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*,
			IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
		using TPresent = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);

		static TD3D11Create g_origCreate = nullptr;
		static std::atomic<TPresent> g_origPresent{ nullptr };
		static std::atomic<IDXGISwapChain*> g_gameSwapChain{ nullptr };
		static std::atomic<bool> g_presentHooked{ false };

		static long long g_qpcFreq = 1;
		static long long g_lastTick = 0;
		static long long g_capTicks = 0;
		static long long g_loadingCapTicks = 0;
		static int g_vsyncMode = 0;
		static UINT g_vsyncInterval = 1;
		static bool g_allowTearing = true;
		static bool g_forceFlip = false;
		static bool g_disableVSyncLoading = true;
		static bool g_tearingSupported = false;

		static long long TicksFor(float a_fps) noexcept
		{
			return a_fps > 0.0f ? static_cast<long long>(static_cast<double>(g_qpcFreq) / a_fps) : 0;
		}

		// Cap the frame: coarse sleep until ~1.5ms out, then spin to the QPC deadline.
		static void Wait(long long a_capTicks) noexcept
		{
			LARGE_INTEGER li;
			::QueryPerformanceCounter(&li);
			long long now = li.QuadPart;
			if (a_capTicks - (now - g_lastTick) > 0)
			{
				const long long deadline = now + (a_capTicks - (now - g_lastTick));
				for (;;)
				{
					::QueryPerformanceCounter(&li);
					now = li.QuadPart;
					if (now >= deadline)
					{
						now = deadline;
						break;
					}

					const long long remainingUs = (deadline - now) * 1000000LL / g_qpcFreq;
					if (remainingUs > 2000)
					{
						std::this_thread::sleep_for(std::chrono::microseconds(remainingUs - 1500));
						continue;
					}

					_mm_pause();
				}
			}
			g_lastTick = now;
		}

		static HRESULT WINAPI Hook_Present(IDXGISwapChain* a_sc, UINT a_syncInterval, UINT a_flags) noexcept
		{
			static const RE::BSFixedString loadingMenu{ "LoadingMenu" };

			const auto orig = g_origPresent.load(std::memory_order_acquire);
			if (a_sc != g_gameSwapChain.load(std::memory_order_relaxed))
				return orig ? orig(a_sc, a_syncInterval, a_flags) : S_OK;

			auto* const ui = RE::UI::GetSingleton();
			const bool loading = ui && ui->GetMenuOpen(loadingMenu);

			long long cap = g_capTicks;
			if (loading && g_loadingCapTicks > 0)
				cap = g_loadingCapTicks;
			if (cap > 0)
				Wait(cap);

			UINT sync = a_syncInterval;
			if (loading && g_disableVSyncLoading)
				sync = 0;
			else if (g_vsyncMode == 1)
				sync = 0;
			else if (g_vsyncMode == 2)
				sync = g_vsyncInterval;

			UINT flags = a_flags;
			if (g_tearingSupported && g_allowTearing && sync == 0)
				flags |= DXGI_PRESENT_ALLOW_TEARING;

			return orig ? orig(a_sc, sync, flags) : S_OK;
		}

		static void QueryTearingSupport(IDXGISwapChain* a_sc) noexcept
		{
			DXGI_SWAP_CHAIN_DESC scDesc{};
			if (FAILED(a_sc->GetDesc(&scDesc)) || !(scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
				return;

			IDXGIFactory* factory = nullptr;
			if (FAILED(a_sc->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) || !factory)
				return;

			IDXGIFactory5* factory5 = nullptr;
			if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5))) && factory5)
			{
				BOOL allow = FALSE;
				if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
					g_tearingSupported = allow != FALSE;
				factory5->Release();
			}
			factory->Release();
		}

		static HRESULT WINAPI Hook_Create(IDXGIAdapter* a_adapter, D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software, UINT a_flags, const D3D_FEATURE_LEVEL* a_levels, UINT a_numLevels,
			UINT a_sdk, const DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_outSc,
			ID3D11Device** a_outDev, D3D_FEATURE_LEVEL* a_outLevel, ID3D11DeviceContext** a_outCtx) noexcept
		{
			DXGI_SWAP_CHAIN_DESC patched{};
			const DXGI_SWAP_CHAIN_DESC* descToUse = a_desc;
			if (g_forceFlip && a_desc)
			{
				patched = *a_desc;
				patched.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				patched.SampleDesc.Count = 1;
				patched.SampleDesc.Quality = 0;
				if (patched.BufferCount < 2)
					patched.BufferCount = 2;
				patched.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
				descToUse = &patched;
			}

			const auto hr = g_origCreate
				? g_origCreate(a_adapter, a_driverType, a_software, a_flags, a_levels, a_numLevels, a_sdk,
					descToUse, a_outSc, a_outDev, a_outLevel, a_outCtx)
				: E_FAIL;

			if (FAILED(hr) || !a_outSc || !*a_outSc)
				return hr;

			bool expected = false;
			if (g_presentHooked.compare_exchange_strong(expected, true))
			{
				g_gameSwapChain.store(*a_outSc, std::memory_order_relaxed);
				QueryTearingSupport(*a_outSc);

				constexpr std::uint32_t kPresentSlot = 8;
				auto** const vtable = *reinterpret_cast<void***>(*a_outSc);
				g_origPresent.store(reinterpret_cast<TPresent>(vtable[kPresentSlot]), std::memory_order_release);

				const auto vtableAddr = reinterpret_cast<std::uintptr_t>(vtable);
				if (RELEX::DetourVTable(vtableAddr, reinterpret_cast<std::uintptr_t>(&Hook_Present), kPresentSlot))
				{
					REX::INFO("Frame Limiter: hooked Present (cap {} fps, vsync {}, tearing {})."sv,
						fFrameLimit.GetValue(), g_vsyncMode, g_tearingSupported);
				}
				else
				{
					REX::WARN("Frame Limiter: Present vtable patch failed."sv);
					g_origPresent.store(nullptr, std::memory_order_release);
					g_presentHooked.store(false);
				}
			}

			return hr;
		}
	}

	ModuleFrameLimiter::ModuleFrameLimiter() :
		Module("Frame Limiter", &bLimiterFrameLimiter)
	{}

	bool ModuleFrameLimiter::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleFrameLimiter::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace frameLimiterDetail;

		LARGE_INTEGER freq;
		::QueryPerformanceFrequency(&freq);
		g_qpcFreq = freq.QuadPart > 0 ? freq.QuadPart : 1;

		g_capTicks = TicksFor(std::clamp(fFrameLimit.GetValue(), 0.0f, 1000.0f));
		g_loadingCapTicks = TicksFor(std::clamp(fLoadingFrameLimit.GetValue(), 0.0f, 1000.0f));
		g_vsyncMode = std::clamp(nVSync.GetValue(), 0, 2);
		g_vsyncInterval = static_cast<UINT>(std::clamp(nVSyncInterval.GetValue(), 1, 4));
		g_allowTearing = bAllowTearing.GetValue();
		g_forceFlip = bForceFlipModel.GetValue();
		g_disableVSyncLoading = bDisableVSyncWhileLoading.GetValue();

		g_origCreate = reinterpret_cast<TD3D11Create>(RELEX::DetourIAT(
			"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<std::uintptr_t>(&Hook_Create)));

		if (!g_origCreate)
		{
			REX::WARN("Frame Limiter: D3D11CreateDeviceAndSwapChain not found in IAT."sv);
			return false;
		}

		return true;
	}

	bool ModuleFrameLimiter::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleFrameLimiter::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
