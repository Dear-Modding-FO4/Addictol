#include <Modules\AdModuleDropItems.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesDropItems{ "Patches", "bDropItems", true };

	ModuleDropItems::ModuleDropItems() :
		Module("Drop Items", &bPatchesDropItems)
	{}

	bool ModuleDropItems::DoQuery() const noexcept
	{
		return !RELEX::IsRuntimeOG();	// OG has a crashing issue when traversing the world
	}

	bool ModuleDropItems::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Extended BSExtraCount 16 -> 32

		// Extended sets new ammo value
		{
			auto idCommon = REL::ID{ 1460465, 2190125 };

			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x30, 0x27 } }.get(), { 0x89, 0xD5, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x36, 0x39 } }.get(), { 0x83, 0xFA, 0x01, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x53 } }.get(), { 0x89, 0x68, 0x18, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x90 } }.get(), { 0x89, 0xEA, 0x90 });
		}

		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 579569,  2191052 }, REL::Offset{ 0x6,   0x1B  } }.get(), { 0x89, 0x51, 0x18, 0x90 });
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 38043,   2190093 }, REL::Offset{ 0x252, 0x417 } }.get(), { 0x8B, 0x57, 0x18, 0x90 });
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 1044135, 2200922 }, REL::Offset{ 0x29F, 0x280 } }.get(), { 0x44, 0x89, 0xF2, 0x90 });
		
		if (!RELEX::IsRuntimeOG())
		{
			RELEX::WriteSafe(REL::Relocation{ REL::ID(2249088), REL::Offset{ 0x11A } }.get(), { 0x44, 0x89, 0xF2, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ REL::ID(2205194), REL::Offset{ 0xB92 } }.get(), { 0x8B, 0x54, 0x30, 0x10, 0x90 });
			// 605470 for OG but.... maybe this need removed
			//RELEX::WriteSafe(REL::Relocation{ REL::ID(2190663), REL::Offset{ 0x02CE } }.get(), { 0x45, 0x8B, 0x75, 0x18, 0x90 });
		}
		else
		{
			RELEX::WriteSafe(REL::Relocation{ REL::ID(75489), REL::Offset{ 0x112 } }.get(), { 0x44, 0x89, 0xFA, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ REL::ID(1492591), REL::Offset{ 0x939 } }.get(), { 0x8B, 0x54, 0x38, 0x10, 0x90 });
		}

		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 185044, 2202658 }, REL::Offset{ 0x122,  0x11D  } }.get(), { 0x8B, 0x51, 0x10, 0x90 });
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 39694,  2190101 }, REL::Offset{ 0x1BB3, 0x246D } }.get(), { 0x8B, 0x55, 0x7F, 0x90 });

		// Skips 0x7FFF
		{
			auto idCommon = REL::ID{ 1267617, 2254270 };
			
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x0B1 } }.get(), { 0x89, 0xC2, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x511, 0x4C1 } }.get(), { 0x89, 0xF2, 0x90, 0x90 });
		}

		// Gets
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 1512810, 2190227 }, REL::Offset{ 0x010 } }.get(), { 0x8B, 0x40, 0x18, 0x90 });
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 785533,  2233039 }, REL::Offset{ 0x37C, 0x309 } }.get(), { 0x41, 0x89, 0xC1, 0x90 });

		// Extended check (correct drop item model)
		RELEX::WriteSafe(REL::Relocation{ REL::ID{ 239100, 2198347 }, REL::Offset{ 0x4C } }.get(), { 0x89, 0xC5, 0x90 });
		
		if (!RELEX::IsRuntimeOG())
		{
			auto idCommon = REL::ID(2197861);

			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x4B } }.get(), { 0x41, 0x89, 0xC0, 0x90 });
			RELEX::WriteSafe(REL::Relocation{ idCommon, REL::Offset{ 0x61 } }.get(), { 0x41, 0x83, 0xF8, 0x01, 0x90 });
		}
		else
			RELEX::WriteSafe(REL::Relocation{ REL::ID{ 531425 }, REL::Offset{ 0x4B } }.get(), { 0x83, 0xF8, 0x01, 0x90 });

		// Many items fix
		if (!RELEX::IsRuntimeOG())
			RELEX::WriteSafe(REL::Relocation{ REL::ID(2200949), REL::Offset{ 0x531 } }.get(), { 0x44, 0x8B, 0x44, 0x24, 0x70, 0x90 });
		else
			RELEX::WriteSafe(REL::Relocation{ REL::ID(78185), REL::Offset{ 0x43E } }.get(), { 0x44, 0x8B, 0x44, 0x24, 0x74, 0x90 });

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