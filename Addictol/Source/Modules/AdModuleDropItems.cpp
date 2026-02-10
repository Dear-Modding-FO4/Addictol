#include <Modules\AdModuleDropItems.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesDropItems{ "Patches", "bDropItems", true };

	ModuleDropItems::ModuleDropItems() :
		Module("Module Drop Items", &bPatchesDropItems)
	{}

	bool ModuleDropItems::DoQuery() const noexcept
	{
		return !RELEX::IsRuntimeOG();	// og no supported
	}

	bool ModuleDropItems::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Extended BSExtraCount 16 -> 32

		if (!RELEX::IsRuntimeOG())
		{
			// Extended sets new ammo value
			{
				auto idCommon = REL::ID(2190125);

				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x0027 } }.get(), { 0x89, 0xD5, 0x90 });
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x0039 } }.get(), { 0x83, 0xFA, 0x01, 0x90 });
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x0053 } }.get(), { 0x89, 0x68, 0x18, 0x90 });
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x0090 } }.get(), { 0x89, 0xEA, 0x90 });
			}

			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2191052), REL::Offset{ 0x001B } }.get(), { 0x89, 0x51, 0x18, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2190093), REL::Offset{ 0x0417 } }.get(), { 0x8B, 0x57, 0x18, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2200922), REL::Offset{ 0x0280 } }.get(), { 0x44, 0x89, 0xF2, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2249088), REL::Offset{ 0x011A } }.get(), { 0x44, 0x89, 0xF2, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2205194), REL::Offset{ 0x0B92 } }.get(), { 0x8B, 0x54, 0x30, 0x10, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2202658), REL::Offset{ 0x011D } }.get(), { 0x8B, 0x51, 0x10, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2190101), REL::Offset{ 0x246D } }.get(), { 0x8B, 0x55, 0x7F, 0x90 });		
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2190663), REL::Offset{ 0x02CE } }.get(), { 0x45, 0x8B, 0x75, 0x18, 0x90 });
			
			// Gets
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2190227), REL::Offset{ 0x0010 } }.get(), { 0x8B, 0x40, 0x18, 0x90 });
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2233039), REL::Offset{ 0x0309 } }.get(), { 0x41, 0x89, 0xC1, 0x90 });

			// Skips 0x7FFF
			{
				auto idCommon = REL::ID(2254270);

				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x0B1 } }.get(), { 0x89, 0xC2, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x4C1 } }.get(), { 0x89, 0xF2, 0x90, 0x90 });
			}

			// Extended check (correct drop item model)
			RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2198347), REL::Offset{ 0x4C } }.get(), { 0x89, 0xC5, 0x90 });
			{
				auto idCommon = REL::ID(2197861);

				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x4B } }.get(), { 0x41, 0x89, 0xC0, 0x90 });
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ idCommon, REL::Offset{ 0x61 } }.get(), { 0x41, 0x83, 0xF8, 0x01, 0x90 });
			}

			if (RELEX::IsRuntimeAE() || !REX::W32::GetModuleHandleA("MentatsF4SE.dll"))
				// Many items fix
				RELEX::WriteSafe(REL::Relocation<uintptr_t>{ REL::ID(2200949), REL::Offset{ 0x531 } }.get(), { 0x44, 0x8B, 0x44, 0x24, 0x70, 0x90 });
		}
		else
		{
			// no supported
			// Task any modmakers with OG game
			// MentatsF4SE.dll dropped more stack items 0x7FFF

			return false;
		}

		return true;
	}

	bool ModuleDropItems::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleDropItems::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}