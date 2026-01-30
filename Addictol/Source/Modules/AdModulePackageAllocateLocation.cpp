#include <Modules\AdModulePackageAllocateLocation.h>
#include <AdUtils.h>
#include <REL\REL.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesPackageAllocateLocation{ "Patches", "bPackageAllocateLocation", true };

	ModulePackageAllocateLocation::ModulePackageAllocateLocation() :
		Module("Module Package Allocate Location", &bPathesPackageAllocateLocation)
	{}

	bool ModulePackageAllocateLocation::DoQuery() const noexcept
	{
		return !RELEX::IsRuntimeOG() /*true if supported OG*/;
	}

	bool ModulePackageAllocateLocation::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			auto Sub = REL::ID(2190427).address();
			RELEX::DetourCall(REL::ID(2211931).address() + 0x144, Sub);
		}
		else
		{
			// OG

			// TODO: Task TheGamersX20 - buffout 4 orig
		}

		return true;
	}
}