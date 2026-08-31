#include <Modules/AdModuleMagicKeywordCrash.h>
#include <Core/AdUtils.h>

#include <RE/E/EffectItem.h>
#include <RE/E/EffectSetting.h>
#include <RE/RTTI.h>

namespace RE
{
	class MagicItemFindKeywordFunctor
	{
	public:
		static constexpr auto RTTI{ RE::RTTI::MagicItemFindKeywordFunctor };
		static constexpr auto VTABLE{ RE::VTABLE::MagicItemFindKeywordFunctor };

		void*       vtable;           // 00
		std::byte   unk008[0x10];     // 08
		BGSKeyword* keyword;          // 18
	};
	static_assert(sizeof(MagicItemFindKeywordFunctor) == 0x20);
}

namespace Addictol
{

	namespace magicKeywordCrashDetail
	{
		struct MatchCondition
		{
			static bool thunk(RE::MagicItemFindKeywordFunctor* a_self, RE::EffectItem* a_effectItem)
			{
				if (!a_effectItem)
					return false;

				auto* effectSetting = a_effectItem->effectSetting;
				// The engine omits the effect-setting null check.
				if (!effectSetting)
					return false;

				return effectSetting->HasKeyword(a_self->keyword, nullptr);
			}
		};
	}

	ModuleMagicKeywordCrash::ModuleMagicKeywordCrash() :
		Module("Magic Keyword Crash", &bFixesMagicKeywordCrash)
	{}

	bool ModuleMagicKeywordCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation vtable{ RE::MagicItemFindKeywordFunctor::VTABLE[0] };
		vtable.write_vfunc(2, magicKeywordCrashDetail::MatchCondition::thunk);

		return true;
	}

}
