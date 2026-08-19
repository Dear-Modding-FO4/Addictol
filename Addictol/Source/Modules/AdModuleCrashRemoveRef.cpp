#include <Modules/AdModuleCrashRemoveRef.h>
#include <Core/AdUtils.h>

#include <RE/T/TESObjectREFR.h>

namespace Addictol
{
	static REX::TOML::Bool<> bCrashRemoveRefFix{ "Fixes"sv, "bCrashRemoveRefFix"sv, true };

	struct BGSObjectVisibilityManager
	{
		// For some reason, the deletion array contains pointers not to REFR, but to some file.

		static void RemoveReference(BGSObjectVisibilityManager* a_this, RE::TESObjectREFR& a_ref) noexcept
		{
			if (a_ref.formType != RE::TESObjectREFR::FORM_ID)
				return;

			RemoveReference_orig(a_this, a_ref);
		}

		inline static decltype(RemoveReference)* RemoveReference_orig{ nullptr };
	};

	ModuleCrashRemoveRef::ModuleCrashRemoveRef() :
		Module("Crash Remove Ref", &bCrashRemoveRefFix)
	{}

	bool ModuleCrashRemoveRef::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		*((uintptr_t*)&BGSObjectVisibilityManager::RemoveReference_orig) = 
			RELEX::DetourJump(REL::ID{ 1099652, 2194273 }.address(),
			reinterpret_cast<uintptr_t>(&BGSObjectVisibilityManager::RemoveReference));

		return BGSObjectVisibilityManager::RemoveReference_orig != nullptr;
	}

}
