#include <Modules\AdModulePackageAllocateLocation.h>
#include <AdUtils.h>
#include <RE\B\BGSPrimitive.h>
#include <RE\E\ExtraDataList.h>

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
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			auto Sub = REL::ID(2190427).address();
			RELEX::DetourCall(REL::ID(2211931).address() + 0x144, Sub);
		}
		else
		{
			// OG
			REL::Relocation<std::uintptr_t> Target{ REL::ID(1248203), 0x141 };

			auto& trampoline = REL::GetTrampoline();
			GetPrimitive::func = trampoline.write_call<5>(Target.address(), GetPrimitive::ExtraDataList_GetPrimitive);
		}

		return true;
	}

	bool ModulePackageAllocateLocation::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePackageAllocateLocation::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}