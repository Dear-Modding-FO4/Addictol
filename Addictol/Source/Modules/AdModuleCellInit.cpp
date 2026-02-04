#include <Modules\AdModuleCellInit.h>
#include <AdUtils.h>

#include <RE/E/ExtraLocation.h>
#include "RE/T/TESFormUtil.h"

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesCellInit{ "Fixes", "bCellInit", true };

	inline RE::BGSLocation* GetLocation(const RE::TESObjectCELL* a_cell)
	{
		const auto xLoc =
			a_cell && a_cell->extraList ?
			a_cell->extraList->GetByType<RE::ExtraLocation>() :
			nullptr;
		auto loc = xLoc ? xLoc->location : nullptr;

		if (loc && a_cell && !a_cell->IsInitialized())
		{
			auto id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(a_cell));
			const auto file = a_cell->GetFile();
			RE::TESForm::AddCompileIndex(id, file);
			loc = RE::TESForm::GetFormByID<RE::BGSLocation>(id);
		}

		return loc;
	}

	struct Patch : Xbyak::CodeGenerator
	{
		explicit Patch(std::uintptr_t a_target)
		{
			mov(rcx, rbx);  // rbx == TESObjectCELL*
			mov(rdx, a_target);
			jmp(rdx);
		}
	};

	ModuleCellInit::ModuleCellInit() :
		Module("Module Cell Init", &bPathesCellInit)
	{}

	bool ModuleCellInit::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleCellInit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target{ REL::ID{ 868663, 2200179 }, REL::Offset(0x3E) };

		Patch p{ reinterpret_cast<std::uintptr_t>(GetLocation) };
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(Target.address(), trampoline.allocate(p));

		return true;
	}

	bool ModuleCellInit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}