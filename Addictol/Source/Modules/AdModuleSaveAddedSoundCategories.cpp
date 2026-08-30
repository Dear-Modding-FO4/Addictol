#include <Modules/AdModuleSaveAddedSoundCategories.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSSoundCategory.h>
#include <RE/S/Setting.h>
#include <RE/T/TESDataHandler.h>

#include <INI/SimpleIni.h>

#define AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES 1

namespace Addictol
{

	namespace saveAddedSoundCategoriesDetail
	{
		// File Name
		constexpr std::string_view FileName = "Data/F4SE/Plugins/Addictol_SNCT.ini";

		// Store
		static CSimpleIniA& GetStore()
		{
			static CSimpleIniA Store;
			return Store;
		}

		// Patch
		struct SaveAddedSoundCategories
		{
			static bool IsMenuCategory(const RE::BGSSoundCategory* soundCategory)
			{
				return (soundCategory->appFlags & 0x2) != 0;
			}

			static bool INIPrefSettingCollection_Unlock(RE::INIPrefSettingCollection* a_self)
			{
				if (const auto dataHandler = RE::TESDataHandler::GetSingleton())
				{
					auto& store = GetStore();

					for (const auto& soundCategory : dataHandler->GetFormArray<RE::BGSSoundCategory>())
					{
						if (IsMenuCategory(soundCategory))
						{
							auto fullName = soundCategory->GetFullName();
							fullName = fullName ? fullName : "";

#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
							REX::INFO("Processing {}"sv, fullName);
							REX::INFO("Menu Flag Set, Saving"sv);
#endif

							auto localFormID = soundCategory->formID & 0x00FFFFFF;

							// ESL
							if ((soundCategory->formID & 0xFF000000) == 0xFE000000)
								localFormID = localFormID & 0x00000FFF;

							const auto srcFile = soundCategory->GetDescriptionOwnerFile();
#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
							REX::INFO("Plugin: {} Form ID: {:08X} Volume: {}"sv, srcFile->filename, localFormID, soundCategory->volumeMult);
#endif

							char localFormIDHex[] = "DEADBEEF";
							sprintf_s(localFormIDHex, std::extent_v<decltype(localFormIDHex)>, "%08X", localFormID);

							store.SetDoubleValue(srcFile->filename, localFormIDHex, soundCategory->volumeMult);
						}
					}

					auto ret = store.SaveFile(FileName.data());

#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
					if (ret < 0)
						REX::WARN("Warning: Unable to save SNCT INI"sv);
					else
						REX::INFO("Saved SNCT Volumes to INI"sv);
#endif
				}

				// Original Function
				a_self->handle = nullptr;
				return true;
			}
		};

		static void LoadVolumes()
		{
#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
			REX::INFO("Loading SNCT Volumes"sv);
#endif

			const auto dataHandler = RE::TESDataHandler::GetSingleton();

			if (dataHandler)
			{
				auto& store = GetStore();
				auto ret = store.LoadFile(FileName.data());

				if (ret < 0)
				{
#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
					REX::WARN("Unable to load SNCT Volume INI"sv);
#endif
					return;
				}

				for (auto& soundCategory : dataHandler->GetFormArray<RE::BGSSoundCategory>())
				{
					auto localFormID = soundCategory->formID & 0x00FFFFFF;

					// ESL
					if ((soundCategory->formID & 0xFF000000) == 0xFE000000)
						localFormID = localFormID & 0x00000FFF;

					char localFormIDHex[] = "DEADBEEF";
					sprintf_s(localFormIDHex, std::extent_v<decltype(localFormIDHex)>, "%08X", localFormID);

					auto srcFile = soundCategory->GetDescriptionOwnerFile();
					auto vol = store.GetDoubleValue(srcFile->filename, localFormIDHex, DBL_MAX);

					if (vol != DBL_MAX)
					{
#if !AD_NOMESSAGE_SAVEADDEDSOUNDCATEGORIES
						REX::INFO("Setting Volume for FormID {:08X} to {}"sv, soundCategory->formID, vol);
#endif
						soundCategory->SetCategoryVolume(static_cast<float>(vol));
					}
				}
			}
		}
	}

	ModuleSaveAddedSoundCategories::ModuleSaveAddedSoundCategories() :
		Module("Save Added Sound Categories", &bPatchesSaveAddedSoundCategories)
	{}

	bool ModuleSaveAddedSoundCategories::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Loading
		saveAddedSoundCategoriesDetail::LoadVolumes();

		// Saving
		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(REL::Relocation{ REL::ID{ 950650, 2274822 } }.address(), saveAddedSoundCategoriesDetail::SaveAddedSoundCategories::INIPrefSettingCollection_Unlock);

		return true;
	}

}