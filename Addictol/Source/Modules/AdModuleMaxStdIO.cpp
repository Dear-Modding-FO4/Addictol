#include <Modules\AdModuleMaxStdIO.h>
#include <AdUtils.h>

namespace Addictol
{
	constexpr inline static auto RESET_MAXSTDIO = -1;
	constexpr inline static auto FATAL_MAXSTDIO = -1;
	constexpr inline static auto DEFAULT_MAXSTDIO = 512;
	constexpr inline static auto MAX_MAXSTDIO = 8 * 1024;

	static REX::TOML::I32<> nFixesMaxStdIO{ "Fixes"sv, "nMaxStdIO"sv, RESET_MAXSTDIO };

	ModuleMaxStdIO::ModuleMaxStdIO() :
		Module("MaxStdIO")
	{}

	bool ModuleMaxStdIO::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMaxStdIO::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto handle = REX::W32::GetModuleHandleA(!RELEX::IsRuntimeOG() ? "api-ms-win-crt-runtime-l1-1-0.dll" : "msvcr110.dll");
		const auto _setmaxio = handle ? reinterpret_cast<decltype(&_setmaxstdio)>(REX::W32::GetProcAddress(handle, "_setmaxstdio")) : nullptr;
		const auto _getmaxio = handle ? reinterpret_cast<decltype(&_getmaxstdio)>(REX::W32::GetProcAddress(handle, "_getmaxstdio")) : nullptr;

		if (!_setmaxio || !_getmaxio)
			return false;

		int nmaxio = _getmaxio();
		int nmaxiouser = nFixesMaxStdIO.GetValue();
		if (nmaxiouser <= RESET_MAXSTDIO)
			nmaxiouser = DEFAULT_MAXSTDIO;

		if (nmaxio != nmaxiouser)
		{
			if (nmaxiouser <= _IOB_ENTRIES)
				nmaxiouser = DEFAULT_MAXSTDIO;

			if (nmaxiouser > MAX_MAXSTDIO)
				nmaxiouser = MAX_MAXSTDIO;

			if (nmaxio == nmaxiouser)
				return true;

			if (_setmaxio(nmaxiouser) == FATAL_MAXSTDIO)
			{
				REX::ERROR("couldn't increase the maximum number of io handles \"{}\""sv, nmaxiouser);
				return false;
			}
			else
				REX::INFO("set max stdio to \"{}\" from \"{}\""sv, nmaxiouser, nmaxio);
		}
		else
			REX::INFO("ignoring set max stdio, now allows \"{}\""sv, nmaxio);

		return true;
	}

	bool ModuleMaxStdIO::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMaxStdIO::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}