#include <Modules/AdModuleMoonRotation.h>
#include <AdUtils.h>

#include <cstring>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMoonRotation{ "Fixes"sv, "bMoonRotation"sv, true };

	ModuleMoonRotation::ModuleMoonRotation() :
		Module("Moon Rotation", &bFixesMoonRotation)
	{}

	bool ModuleMoonRotation::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMoonRotation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Flip the imm8 0x04 -> 0x03 in Moon::Init's `or word ptr [node+0x140], 4` to fix lunar disc orientation; NG/AE share +0x1F7, OG is +0x1E2.
		const auto target = REL::Relocation<std::uintptr_t>{
			REL::ID{ 114988, 2208804, 2208804 }, REL::Offset{ 0x1E2, 0x1F7, 0x1F7 } }.address();

		// Bail unless the imm8 is 0x04 followed by `mov rcx, [rsi+8]` (48 8B 4E 08), identical on OG/NG/AE.
		static constexpr std::uint8_t expected[] = { 0x04, 0x48, 0x8B, 0x4E, 0x08 };
		if (std::memcmp(reinterpret_cast<const void*>(target), expected, sizeof(expected)) != 0)
		{
			REX::WARN("Moon Rotation: unexpected bytes at target -- skipping to avoid corruption."sv);
			return false;
		}

		RELEX::WriteSafe(target, { 0x03 });
		return true;
	}

	bool ModuleMoonRotation::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMoonRotation::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
