#include <Modules/AdModuleEffectItemConditions.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	// A dropped effect left the loader's current item on the previous one.
	namespace effectItemConditionsDetail
	{
		// mov rbx, rax; test rax, rax; jz rel32 after the EffectSetting cast
		constexpr std::initializer_list<uint8_t> kCastFailBranch{ 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84 };

		[[nodiscard]] static uintptr_t BranchTarget(uintptr_t a_branch) noexcept
		{
			return a_branch + 6 + *reinterpret_cast<const int32_t*>(a_branch + 2);
		}

		// mov [current], rbx; rbx is the null cast result on the failed branch.
		[[nodiscard]] static bool IsCurrentStore(uintptr_t a_store, uintptr_t a_current) noexcept
		{
			return RELEX::Validate(a_store, { 0x48, 0x89, 0x1D }) &&
				a_store + 7 + *reinterpret_cast<const int32_t*>(a_store + 3) == a_current;
		}

		static void Retarget(uintptr_t a_branch, uintptr_t a_store) noexcept
		{
			REL::WriteSafeData(a_branch + 2, static_cast<int32_t>(a_store - (a_branch + 6)));
		}
	}

	ModuleEffectItemConditions::ModuleEffectItemConditions() :
		Module("Effect Item Conditions", &bFixesEffectItemConditions)
	{}

	bool ModuleEffectItemConditions::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace effectItemConditionsDetail;

		const auto current = RE::ID::MagicItem::CurrentLoadEffect.address();

		// The store falls through into the branch's original target.
		const auto listBranch = REL::Relocation<uintptr_t>{ RE::ID::MagicItem::LoadEffectItemList, 0x8A }.address();
		const auto listStore = RELEX::Validate(listBranch - 6, kCastFailBranch) ? BranchTarget(listBranch) - 7 : 0;
		if (!listStore || !IsCurrentStore(listStore, current))
		{
			Skip("LoadEffectItemList cast branch bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		// OG's Load calls LoadEffectItemList; NG/AE inline a copy that jumps to the target.
		uintptr_t loadBranch{ 0 };
		uintptr_t loadStore{ 0 };
		if (!RELEX::IsRuntimeOG())
		{
			loadBranch = REL::Relocation<uintptr_t>{ RE::ID::MagicItem::Load, 0x1A2 }.address();
			loadStore = REL::Relocation<uintptr_t>{ RE::ID::MagicItem::Load, 0x24A }.address();
			if (!RELEX::Validate(loadBranch - 6, kCastFailBranch) || !IsCurrentStore(loadStore, current) ||
				!RELEX::Validate(loadStore + 7, { 0xEB }) ||
				loadStore + 9 + *reinterpret_cast<const int8_t*>(loadStore + 8) != BranchTarget(loadBranch))
			{
				Skip("MagicItem::Load cast branch bytes do not match; unsupported runtime or another plugin owns the site"sv);
				return false;
			}
		}

		Retarget(listBranch, listStore);
		if (loadBranch)
			Retarget(loadBranch, loadStore);

		return true;
	}
}
