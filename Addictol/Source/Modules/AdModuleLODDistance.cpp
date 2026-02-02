#include <Modules\AdModuleLODDistance.h>
#include <AdUtils.h>
#include <REL\REL.h>
#include <RE\IDs.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesLODDistance{ "Fixes", "bLODDistance", true };

	ModuleLODDistance::ModuleLODDistance() :
		Module("Module LOD Distance", &bPathesLODDistance)
	{}

	bool ModuleLODDistance::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLODDistance::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		//
		// It is required because many STATs that are also workshop buildable have LOD
		// If you add the flag, it causes bug above.
		//
		// Sets as always disabled

		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE

			RELEX::WriteSafe(REL::ID(2200804).address(), { 0x31, 0xC0, 0xC3, 0x90 });	// xor eax, eax; ret;
			RELEX::WriteSafe(REL::ID(2200805).address(), { 0x31, 0xC0, 0xC3, 0x90 });	// xor eax, eax; ret;
			RELEX::WriteSafe(REL::ID(2201047).address(), { 0x31, 0xC0, 0xC3, 0x90 });	// xor eax, eax; ret;
			RELEX::WriteSafe(REL::ID(2200806).address(), { 0x31, 0xC0, 0xC3, 0x90 });	// xor eax, eax; ret;

			auto Target = REL::ID(2201169).address();
			RELEX::WriteSafe(Target + 0x6B, { 0xEB, 0x2C, 0x90 });
			RELEX::WriteSafe(Target + 0xCC, { 0xEB, 0x28, 0x90 });
			
			RELEX::WriteSafe(REL::ID(2197513).address() + 0x5B, { 0xEB });
		}
		else
		{
			RELEX::WriteSafe(REL::ID(375876).address(), { 0x31, 0xC0, 0xC3, 0x90 });		// xor eax, eax; ret;
			RELEX::WriteSafe(REL::ID(109874).address(), { 0x31, 0xC0, 0xC3, 0x90 });		// xor eax, eax; ret;
			RELEX::WriteSafe(REL::ID(1239016).address(), { 0x31, 0xC0, 0xC3, 0x90 });		// xor eax, eax; ret;
			RELEX::WriteSafe(REL::Offset(0x52A80).address(), { 0x31, 0xC0, 0xC3, 0x90 });	// xor eax, eax; ret;

			auto Target = REL::ID(921453).address();
			RELEX::WriteSafe(Target + 0x6B, { 0xEB, 0x2C, 0x90 });
			RELEX::WriteSafe(Target + 0xCC, { 0xEB, 0x28, 0x90 });

			RELEX::WriteSafe(REL::ID(750853).address() + 0x6C, { 0xEB });
		}

		return true;
	}

	bool ModuleLODDistance::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}