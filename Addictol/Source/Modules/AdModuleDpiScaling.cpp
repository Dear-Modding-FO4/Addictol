#include <Modules/AdModuleDpiScaling.h>
#include <AdUtils.h>

#include <windows.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesDpiScaling{ "Patches"sv, "bDpiScaling"sv, true };

	namespace detail
	{
		// Win10 1703+ context value; declared locally to avoid bumping the Windows SDK floor.
		static const HANDLE kPerMonitorAwareV2 = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4));
		static constexpr int kProcessPerMonitorDpiAware = 2;

		using TSetProcessDpiAwarenessContext = BOOL(WINAPI*)(HANDLE);
		using TSetProcessDpiAwareness = HRESULT(WINAPI*)(int);
		using TSetProcessDpiAware = BOOL(WINAPI*)();

		[[nodiscard]] static bool TrySetPerMonitorV2() noexcept
		{
			auto user32 = GetModuleHandleW(L"user32.dll");
			if (!user32)
				return false;
			auto fn = reinterpret_cast<TSetProcessDpiAwarenessContext>(
				GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
			if (!fn)
				return false;
			// ACCESS_DENIED means the process is already DPI-aware via manifest / compat shim; treat as success.
			return fn(kPerMonitorAwareV2) || GetLastError() == ERROR_ACCESS_DENIED;
		}

		[[nodiscard]] static bool TrySetPerMonitor() noexcept
		{
			auto shcore = GetModuleHandleW(L"shcore.dll");
			if (!shcore)
				shcore = LoadLibraryW(L"shcore.dll");
			if (!shcore)
				return false;
			auto fn = reinterpret_cast<TSetProcessDpiAwareness>(
				GetProcAddress(shcore, "SetProcessDpiAwareness"));
			if (!fn)
				return false;
			const HRESULT hr = fn(kProcessPerMonitorDpiAware);
			return SUCCEEDED(hr) || hr == E_ACCESSDENIED;
		}

		[[nodiscard]] static bool TrySetDpiAware() noexcept
		{
			auto user32 = GetModuleHandleW(L"user32.dll");
			if (!user32)
				return false;
			auto fn = reinterpret_cast<TSetProcessDpiAware>(
				GetProcAddress(user32, "SetProcessDPIAware"));
			if (!fn)
				return false;
			return fn() != FALSE;
		}
	}

	ModuleDpiScaling::ModuleDpiScaling() :
		Module("DPI Scaling", &bPatchesDpiScaling)
	{}

	bool ModuleDpiScaling::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		std::string level = "";
		if (detail::TrySetPerMonitorV2())
			level = "PER_MONITOR_AWARE_V2"sv;
		else if (detail::TrySetPerMonitor())
			level = "PROCESS_PER_MONITOR_DPI_AWARE"sv;
		else if (detail::TrySetDpiAware())
			level = "DPI_AWARE"sv;

		if (!level.empty())
			REX::INFO("DPI Scaling: applied {}"sv, level);
		else
			REX::WARN("DPI Scaling: no SetProcessDpi* entry point accepted; process remains DPI-unaware"sv);

		// Always succeed; DPI-unaware is the vanilla behavior, not a fatal install error.
		return true;
	}

}
