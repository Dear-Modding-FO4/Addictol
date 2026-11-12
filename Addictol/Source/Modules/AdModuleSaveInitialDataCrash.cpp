#include <Modules/AdModuleSaveInitialDataCrash.h>
#include <Core/AdUtils.h>

#include <array>

#include <xbyak/xbyak.h>

#undef ERROR

namespace Addictol
{
	namespace saveInitialDataCrashDetail
	{
		static inline REL::Relocation<uintptr_t> SaveInitialData{ RE::ID::BGSSaveLoadInitialData::SaveInitialData };

		// A ref with no cell has no save location; UnloadForm expects that.
		struct LocationGuard : Xbyak::CodeGenerator
		{
			LocationGuard(uintptr_t a_setNumericID, uintptr_t a_resume)
			{
				Xbyak::Label setLab;
				Xbyak::Label resumeLab;

				// OG loads the index after the form ID; NG/AE load it before.
				if (RELEX::IsRuntimeOG())
					lea(rcx, ptr[rbp - 9]);

				// Leave the zeroed index, as the editor-location fallback does.
				test(rax, rax);
				jz("skip");
				mov(edx, dword[rax + 0x14]);
				call(ptr[rip + setLab]);

				L("skip");
				jmp(ptr[rip + resumeLab]);

				L(setLab);
				dq(a_setNumericID);

				L(resumeLab);
				dq(a_resume);
			}
		};
	}

	ModuleSaveInitialDataCrash::ModuleSaveInitialDataCrash() :
		Module("Save Initial Data Crash", &bFixesSaveInitialDataCrash)
	{}

	bool ModuleSaveInitialDataCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace saveInitialDataCrashDetail;

		const auto function = SaveInitialData.address();
		const auto setNumericID = REL::Relocation<uintptr_t>{ RE::ID::BGSNumericIDIndex::SetNumericID }.address();
		const bool og = RELEX::IsRuntimeOG();

		const std::array<uintptr_t, 3> sites
		{
			function + REL::Offset{ 0x149, 0x15A }.offset(),	// moved reference
			function + REL::Offset{ 0x1AC, 0x1CD }.offset(),	// created reference
			function + REL::Offset{ 0x247, 0x278 }.offset(),	// reference whose cell changed
		};
		const size_t callOffset = og ? 7 : 3;

		for (const auto site : sites)
		{
			const bool vanilla = og ?
				RELEX::Validate(site, { 0x8B, 0x50, 0x14, 0x48, 0x8D, 0x4D, 0xF7 }) :	// mov edx, [rax+14h]; lea rcx, [rbp-9]
				RELEX::Validate(site, { 0x8B, 0x50, 0x14 });							// mov edx, [rax+14h]
			if (!vanilla || !RELEX::IsCallTo(site + callOffset, setNumericID))
			{
				Skip("a SaveInitialData location read does not match; unsupported runtime or another plugin patched it"sv);
				return false;
			}
		}

		for (const auto site : sites)
		{
			if (RELEX::XbyakJump<LocationGuard>(site, setNumericID, site + callOffset + 5) == 0)
			{
				REX::ERROR("Save Initial Data Crash: failed to patch the save location read at 0x{:X}"sv, site);
				return false;
			}
		}

		return true;
	}
}
