#include <Modules/AdModuleProcessIcon.h>
#include <AdUtils.h>

#include <windows.h>

namespace Addictol
{
	static ATOM BSGraphics__InitWindows__RegisterClassA(WNDCLASSA* lpWndClass)
	{
		// Assign the loaded icon handles
		lpWndClass->hIcon = LoadIcon(lpWndClass->hInstance, MAKEINTRESOURCE(101));	// Taskbar (Large)
		return RegisterClassA(lpWndClass);
	}

	ModuleProcessIcon::ModuleProcessIcon() :
		Module("Process Icon")
	{}

	bool ModuleProcessIcon::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleProcessIcon::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RELEX::DetourCall(REL::Relocation(REL::ID{ 193854, 2276814 }, REL::Offset{ 0x92, 0x194 }).address(),
			reinterpret_cast<uintptr_t>(&BSGraphics__InitWindows__RegisterClassA));

		return true;
	}

	bool ModuleProcessIcon::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleProcessIcon::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}