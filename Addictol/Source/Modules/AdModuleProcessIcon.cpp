#include <Modules/AdModuleProcessIcon.h>
#include <Core/AdUtils.h>

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

	bool ModuleProcessIcon::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RELEX::DetourCall(REL::Relocation(REL::ID{ 193854, 2276814 }, REL::Offset{ 0x92, 0x194 }).address(),
			reinterpret_cast<uintptr_t>(&BSGraphics__InitWindows__RegisterClassA));

		return true;
	}

}