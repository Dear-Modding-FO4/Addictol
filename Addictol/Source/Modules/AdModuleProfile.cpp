#include <Modules\AdModuleProfile.h>
#include <AdUtils.h>
#include <RE\S\Setting.h>
#include <REL\REL.h>
#include <RE\IDs.h>
#include <detours\Detours.h>

#include <windows.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesProfile{ "Patches", "bProfile", true };

	static bool hk_nullsub_C30008()
	{
		auto iniDef = RE::INISettingCollection::GetSingleton();
		auto iniPref = RE::INIPrefSettingCollection::GetSingleton();

		auto& pSettingSrc = iniPref->settings;
		for (auto Setting : pSettingSrc)
		{
			if (Setting)
			{
				auto pFound = iniDef->GetSetting(Setting->GetKey());
				if (!pFound)
					iniDef->settings.push_front(Setting);
			}
		}

		return false;
	}

	ModuleProfile::ModuleProfile() :
		Module("Module Profile", &bPathesProfile)
	{}

	bool ModuleProfile::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleProfile::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE

			auto Target = REL::ID(2228907).address();
			//REX::INFO("[DBG]\tTarget 0x{:X}", Target - REL::Module::GetSingleton()->base());
			Detours::X64::DetourFunction(Target + (RELEX::IsRuntimeNG() ? 0x4A7 : 0x538), 
				(uintptr_t)&hk_nullsub_C30008, Detours::X64Option::USE_REL32_CALL);
		}
		else
		{
			// OG

			(*const_cast<REL::ID*>(&RE::ID::Setting::INISettingCollection::Singleton)) = 791183;
			(*const_cast<REL::ID*>(&RE::ID::Setting::INIPrefSettingCollection::Singleton)) = 767844;
	
			auto Target = REL::ID(665510).address();
			Detours::X64::DetourFunction(Target + 0x3BE, (uintptr_t)&hk_nullsub_C30008, Detours::X64Option::USE_REL32_CALL);
		}

		return true;
	}
}