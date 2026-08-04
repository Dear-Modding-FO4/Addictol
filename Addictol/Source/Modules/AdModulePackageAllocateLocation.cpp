#include <Modules/AdModulePackageAllocateLocation.h>
#include <AdUtils.h>
#include <RE/B/BGSPrimitive.h>
#include <RE/E/ExtraDataList.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPackageAllocateLocation{ "Fixes"sv, "bPackageAllocateLocation"sv, true };

	struct GetPrimitive
	{
		[[nodiscard]] inline static RE::BGSPrimitive* ExtraDataList_GetPrimitive(const RE::ExtraDataList* a_this) noexcept
		{
			return a_this ? func(a_this) : nullptr;
		}

		static inline REL::Relocation<decltype(ExtraDataList_GetPrimitive)> func;
	};

	ModulePackageAllocateLocation::ModulePackageAllocateLocation() :
		Module("Package Allocate Location", &bFixesPackageAllocateLocation)
	{}

	bool ModulePackageAllocateLocation::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePackageAllocateLocation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation Target{ REL::ID{ 1248203, 2211931 }, REL::Offset{ 0x141, 0x144 } };
		if (!RELEX::Validate(Target, { 0xE8 })) return false;
		GetPrimitive::func = RELEX::DetourClassCall(Target, &GetPrimitive::ExtraDataList_GetPrimitive);
		return GetPrimitive::func != 0;
	}

	bool ModulePackageAllocateLocation::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePackageAllocateLocation::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}