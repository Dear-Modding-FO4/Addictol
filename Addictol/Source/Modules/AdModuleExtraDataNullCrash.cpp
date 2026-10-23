#include <Modules/AdModuleExtraDataNullCrash.h>
#include <Core/AdUtils.h>
#include <Core/Settings/AdSettings.h>

#include <RE/E/EXTRA_DATA_TYPE.h>
#include <RE/E/ExtraDataList.h>

namespace Addictol
{
	namespace extraDataNullCrashDetail
	{
		struct ExtraDataList
		{
			static RE::BSExtraData* GetExtraData(RE::ExtraDataList* a_this, RE::EXTRA_DATA_TYPE a_type) noexcept
			{
				if (!a_this)
					return nullptr;

				return GetExtraData_orig(a_this, a_type);
			}

			inline static decltype(GetExtraData)* GetExtraData_orig{ nullptr };
		};
	}

	ModuleExtraDataNullCrash::ModuleExtraDataNullCrash() :
		Module("ExtraDataList Null Crash", &bFixesExtraDataNullCrash)
	{}

	bool ModuleExtraDataNullCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Addresses: 1.10.163: ID 443422, 1.10.984: ID 2212354, 1.11.240: ID 44643616
		const auto target = REL::Relocation{ REL::ID{ 443422, 2212354, 44643616 } }.address();

		*((uintptr_t*)&extraDataNullCrashDetail::ExtraDataList::GetExtraData_orig) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&extraDataNullCrashDetail::ExtraDataList::GetExtraData));

		return extraDataNullCrashDetail::ExtraDataList::GetExtraData_orig != nullptr;
	}
}
