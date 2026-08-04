#include <Modules/AdModuleCOMInit.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesCOMInit{ "Patches"sv, "bCOMInit"sv, true };

	namespace detail
	{
		static REX::W32::HRESULT CoInitializeEx([[maybe_unused]] void* pvReserved, [[maybe_unused]] uint32_t dwCoInit);
		static inline REL::Relocation<decltype(CoInitializeEx)> CoInitializeExOrig;

		static REX::W32::HRESULT CoInitializeEx([[maybe_unused]] void* pvReserved, [[maybe_unused]] uint32_t dwCoInit)
		{
			// analog CoInitialize(nullptr)
			return CoInitializeExOrig(nullptr, 2);
		}
	}

	ModuleCOMInit::ModuleCOMInit() :
		Module("COM Init", &bPatchesCOMInit)
	{}

	bool ModuleCOMInit::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleCOMInit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto dll = REX::W32::GetModuleHandleA("Ole32.dll");
		if (!dll)
		{
			REX::INFO("No found Ole32.dll"sv);
			return false;
		}

		auto func = REX::W32::GetProcAddress(dll, "CoInitializeEx");
		if (!func)
		{
			REX::INFO("No found CoInitializeEx() in Ole32.dll"sv);
			return false;
		}
		
		detail::CoInitializeExOrig = RELEX::DetourJump(reinterpret_cast<uintptr_t>(func), 
			reinterpret_cast<uintptr_t>(&detail::CoInitializeEx));

		if (!detail::CoInitializeExOrig)
		{
			REX::INFO("Fatal patching Ole32.dll"sv);
			return false;
		}

		return true;
	}

	bool ModuleCOMInit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleCOMInit::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}