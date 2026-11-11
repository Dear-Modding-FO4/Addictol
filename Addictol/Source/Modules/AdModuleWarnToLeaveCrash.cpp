// Thanks to Yuof for finding this bug in Skyrim: https://github.com/Yuof/CrashFixCompendium
#include <Modules/AdModuleWarnToLeaveCrash.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	namespace warnToLeaveCrashDetail
	{
		using SetParamByUID_t = uintptr_t(void*, uint32_t, uint8_t);

		inline constexpr uint32_t kUntilWithinLocation = 2;
		inline constexpr uint8_t kNoData = 0xFF;

		static SetParamByUID_t* s_setParamByUID = nullptr;

		// The warn-to-leave tree's Pursue maps parameters 0, 1 and 3 but never 2 (UntilWithinLocation):
		//
		//     SetParamByUID(params, 0, 0)    ; Target
		//     SetParamByUID(params, 1, 1)    ; WhileWithinLocation
		//     SetParamByUID(params, 3, 2)    ; WarnToLeave, grows the array to 4 uninitialized slots
		//
		// Slot 2 keeps leftover heap bytes. When they read as uid 2, the package's Bool, AllocateLocation
		// steps back 8 bytes to a location interface the Bool does not have and calls through garbage.
		// No data is what any leftover uid of 3 or more already gives: Pursue runs without an until place.
		static uintptr_t HKSetParamByUID(void* a_params, uint32_t a_index, uint8_t a_uid) noexcept
		{
			const auto result = s_setParamByUID(a_params, a_index, a_uid);
			s_setParamByUID(a_params, kUntilWithinLocation, kNoData);
			return result;
		}
	}

	ModuleWarnToLeaveCrash::ModuleWarnToLeaveCrash() :
		Module("Warn To Leave Crash", &bFixesWarnToLeaveCrash)
	{}

	bool ModuleWarnToLeaveCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace warnToLeaveCrashDetail;

		// The builder's last mapping, SetParamByUID(params, 3, 2); every caller goes through it.
		const auto site = REL::Relocation<uintptr_t>{ RE::ID::TESCustomPackageData::BuildWarnToLeaveProcedureTree,
			REL::Offset{ 0x13B, 0xA7 } }.address();
		const auto setParamByUID = REL::Relocation<uintptr_t>{ RE::ID::BGSProcedureBase::ParameterArray::SetParamByUID }.address();

		// mov edx, 3 right before the call, and mov r8b, 2 ahead of it (OG orders the lea rcx differently)
		const auto uidAt = RELEX::IsRuntimeOG() ? site - 8 : site - 12;
		if (!RELEX::IsCallTo(site, setParamByUID) || !RELEX::Validate(site - 5, { 0xBA, 0x03, 0x00, 0x00, 0x00 }) ||
			!RELEX::Validate(uidAt, { 0x41, 0xB0, 0x02 }))
		{
			Skip("the warn-to-leave tree builder does not match; unsupported runtime or another plugin patched it"sv);
			return false;
		}

		s_setParamByUID = reinterpret_cast<SetParamByUID_t*>(RELEX::DetourCall(site, reinterpret_cast<uintptr_t>(&HKSetParamByUID)));
		return s_setParamByUID != nullptr;
	}
}
