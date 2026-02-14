#include <Modules\AdModuleINISettingCollection.h>
#include <AdUtils.h>

#include <RE/S/Setting.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesINISettingCollection{ "Patches"sv, "bINISettingCollection"sv, true };

	struct Open
	{
		static bool thunk(RE::INISettingCollection& a_self, bool a_write)
		{
			return std::filesystem::exists(a_self.settingFile) ? func(a_self, a_write) : false;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	ModuleINISettingCollection::ModuleINISettingCollection() :
		Module("INISettingCollection", &bPatchesINISettingCollection)
	{}

	bool ModuleINISettingCollection::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleINISettingCollection::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::INISettingCollection::VTABLE[0] };
		Open::func = vtable.write_vfunc(0x5, Open::thunk);

		return true;
	}

	bool ModuleINISettingCollection::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleINISettingCollection::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}