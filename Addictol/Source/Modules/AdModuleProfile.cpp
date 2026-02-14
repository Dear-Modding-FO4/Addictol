#include <Modules\AdModuleProfile.h>
#include <AdUtils.h>
#include <RE\S\Setting.h>

#define strcasecmp _stricmp

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesProfile{ "Patches"sv, "bProfile"sv, true };

	static bool hk_nullsub_C30008() noexcept
	{
		auto iniDef = RE::INISettingCollection::GetSingleton();
		auto iniPref = RE::INIPrefSettingCollection::GetSingleton();
		auto& pSettingSrc = iniPref->settings;

		for (auto setting : pSettingSrc)
		{
			auto findSetting = iniDef->GetSetting(setting->GetKey());
			if (!findSetting)
				iniDef->settings.push_front(setting);
		}

		return false;
	}

	ModuleProfile::ModuleProfile() :
		Module("Profile", &bPatchesProfile)
	{}

	bool ModuleProfile::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleProfile::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RELEX::DetourCall(REL::Relocation{ REL::ID{ 665510, 2228907 }, REL::Offset{ 0x3BE, 0x4A7, 0x538 } }.get(), 
			(uintptr_t)&hk_nullsub_C30008);

		return true;
	}

	bool ModuleProfile::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleProfile::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}