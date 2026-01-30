#include <Modules\AdModuleProfile.h>
#include <AdUtils.h>
#include <RE\S\Setting.h>
#include <REL\REL.h>
#include <RE\IDs.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesProfile{ "Patches", "bProfile", true };

	static bool hk_nullsub_C30008() noexcept
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
			RELEX::DetourCall(Target + (RELEX::IsRuntimeNG() ? 0x4A7 : 0x538), (uintptr_t)&hk_nullsub_C30008);
		}
		else
		{
			// OG

			RELEX::UpdateID(RE::ID::Setting::INISettingCollection::Singleton, 791183);
			RELEX::UpdateID(RE::ID::Setting::INIPrefSettingCollection::Singleton, 767844);

			auto Target = REL::ID(665510).address();
			RELEX::DetourCall(Target + 0x3BE, (uintptr_t)&hk_nullsub_C30008);
		}

		return true;
	}
}