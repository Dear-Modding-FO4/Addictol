#include <Modules/AdModuleMoonRotation.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMoonRotation{ "Fixes"sv, "bMoonRotation"sv, true };

	ModuleMoonRotation::ModuleMoonRotation() :
		Module("Moon Rotation", &bFixesMoonRotation)
	{}

	bool ModuleMoonRotation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation{ REL::ID{ 114988, 2208804 }, REL::Offset{ 0x1E2, 0x1F7 } }.address();
		if (!RELEX::Validate(target, { 0x04, 0x48, 0x8B, 0x4E, 0x08 }))
		{
			REX::WARN("Moon Rotation: unexpected bytes at target -- skipping to avoid corruption."sv);
			return false;
		}

		// Flip the imm8 0x04 -> 0x03 in Moon::Init's or word ptr [node+0x140], 4.
		RELEX::WriteSafe(target, { 0x03 });
		return true;
	}

}
