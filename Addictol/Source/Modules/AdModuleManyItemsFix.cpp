#include <Modules/AdModuleManyItemsFix.h>
#include <AdUtils.h>
#include <AdAssert.h>

#include <RE/T/TESObjectREFR.h>
#include <RE/E/ExtraDataList.h>
#include <RE/N/NiPoint3.h>
#include <RE/M/MemoryManager.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesManyItems{ "Fixes"sv, "bManyItems"sv, true };

	using TDropItemIntoWorld = uint32_t* (*)(RE::TESObjectREFR*, uint32_t*, RE::TESBoundObject*, int32_t, RE::TESObjectREFR*,
		RE::NiPoint3*, RE::NiPoint3*, RE::ExtraDataList*);
	using TExtraDataList__Ctor = void (*)(RE::ExtraDataList*);
	using TExtraDataList__CopyExtraList = void (*)(RE::ExtraDataList*, RE::ExtraDataList*);
	using TExtraDataList__SetCount = void (*)(RE::ExtraDataList*, int16_t);

	TDropItemIntoWorld DropItemIntoWorld_orig = nullptr;
	TExtraDataList__Ctor ExtraDataList__Ctor = nullptr;
	TExtraDataList__CopyExtraList ExtraDataList__CopyExtraList_orig = nullptr;
	TExtraDataList__SetCount ExtraDataList__SetCount_orig = nullptr;

	namespace detail
	{
		static uint32_t* HookDropItemIntoWorld(RE::TESObjectREFR* a_refr, uint32_t* a_handle, RE::TESBoundObject* a_item,
			int32_t a_count, RE::TESObjectREFR* a_container, RE::NiPoint3* a_pa, RE::NiPoint3* a_pb, RE::ExtraDataList* a_extra) noexcept
		{
			while (a_count >= 0x8000)
			{
				a_count -= 0x7FFF;

				auto& memMgr = RE::MemoryManager::GetSingleton();
				auto extraList = (RE::ExtraDataList*)memMgr.Allocate(sizeof(RE::ExtraDataList), 16, true);
				if (!extraList)
					break;

				ExtraDataList__Ctor(extraList);
				ExtraDataList__CopyExtraList_orig(a_extra, extraList);
				ExtraDataList__SetCount_orig(extraList, 0x7FFF);
				DropItemIntoWorld_orig(a_refr, a_handle, a_item, 0x7FFF, a_container, a_pa, a_pb, extraList);
			}

			ExtraDataList__SetCount_orig(a_extra, a_count);
			DropItemIntoWorld_orig(a_refr, a_handle, a_item, a_count, a_container, a_pa, a_pb, a_extra);

			return a_handle;
		}
	}

	ModuleManyItemsFix::ModuleManyItemsFix() :
		Module("Many Items", &bFixesManyItems)
	{}

	bool ModuleManyItemsFix::DoQuery() const noexcept
	{
		if (RELEX::IsRuntimeOG() && IsModDLLPresent("Drop7FFFPatch.dll"))
		{
			Skip("Standalone 'Drop7FFFPatch.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleManyItemsFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		*(uintptr_t*)&DropItemIntoWorld_orig = REL::ID{ 1044135, 2200922 }.address();
		*(uintptr_t*)&ExtraDataList__Ctor = REL::ID{ 1329859, 2190088 }.address();
		*(uintptr_t*)&ExtraDataList__CopyExtraList_orig = REL::ID{ 561304, 2190094 }.address();
		*(uintptr_t*)&ExtraDataList__SetCount_orig = REL::ID{ 1460465, 2190125 }.address();

		// Drop many items fix
		RELEX::DetourCall(REL::Relocation{ REL::ID{ 943233, 2200919 }, REL::Offset{ 0x3BB, 0x541 } }.get(),
			(uintptr_t)&detail::HookDropItemIntoWorld);

		// Many items fix to container
		if (!RELEX::IsRuntimeOG())
			RELEX::WriteSafe(REL::Relocation{ REL::ID(2200949), REL::Offset{ 0x531 } }.get(), { 0x44, 0x8B, 0x44, 0x24, 0x70, 0x90 });
		else
			RELEX::WriteSafe(REL::Relocation{ REL::ID(78185), REL::Offset{ 0x43E } }.get(), { 0x44, 0x8B, 0x44, 0x24, 0x74, 0x90 });

		return true;
	}

}