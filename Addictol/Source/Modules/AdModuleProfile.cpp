#include <Modules\AdModuleProfile.h>
#include <AdUtils.h>
#include <RE\S\Setting.h>

#define strcasecmp _stricmp

namespace Addictol
{
	static REX::TOML::Bool<> bPathesProfile{ "Patches", "bProfile", true };

	class SettingCollection
	{
	public:
		virtual ~SettingCollection();

		virtual void Unk_01() = 0;
		virtual void Unk_02() = 0;
		virtual void Unk_03() = 0;
		virtual void Unk_04() = 0;
		virtual bool Unk_05();
		virtual bool Unk_06();
		virtual bool Unk_07();
		virtual bool Unk_08();		// return unk110 != 0;
		virtual bool Unk_09();		// return unk110 != 0;

		//	void	** _vtbl;		// 000
		char	unk004[260];		// 008
		uint32_t pad10C;			// 10C
		void* unk110;				// 110
	};


	class SettingCollectionList : public SettingCollection
	{
	public:
		virtual ~SettingCollectionList();

		// this is almost certainly a templated linked list type
		struct Node
		{
			RE::Setting* data;
			Node* next;
		};

		void* unk118;	// 118
		Node* data;		// 120

		RE::Setting* Get(const char* name) const noexcept
		{
			Node* node = data;
			do
			{
				RE::Setting* setting = node->data;
				if (setting)
				{
					std::string_view searchName(name);
					std::string_view settingName(setting->GetKey());
					if (strcasecmp(searchName.data(), settingName.data()))
						return setting;
				}

				node = node->next;
			} while (node);

			return nullptr;
		}
	};

	static bool hk_nullsub_C30008() noexcept
	{
		auto iniDef = (SettingCollectionList*)RELEX::GetTSingletonByID<RE::INISettingCollection>(2704108, 2704108, 791183);
		auto iniPref = (SettingCollectionList*)RELEX::GetTSingletonByID<RE::INIPrefSettingCollection>(2703234, 2703234, 767844);
		auto pSettingSrc = iniPref->data;

		do
		{
			auto pSettingDst = iniDef->Get(pSettingSrc->data->GetKey().data());
			if (!pSettingDst)
			{
				auto pNewNode = new SettingCollectionList::Node;
				pNewNode->next = iniDef->data;
				pNewNode->data = pSettingSrc->data;
				iniDef->data = pNewNode;
			}

		} while (pSettingSrc = pSettingSrc->next);

		return false;
	}

	ModuleProfile::ModuleProfile() :
		Module("Module Profile", &bPathesProfile)
	{}

	bool ModuleProfile::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleProfile::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
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

			auto Target = REL::ID(665510).address();
			RELEX::DetourCall(Target + 0x3BE, (uintptr_t)&hk_nullsub_C30008);
		}

		return true;
	}

	bool ModuleProfile::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}