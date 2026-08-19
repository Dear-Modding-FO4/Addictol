#include <Modules/AdModuleUnalignedLoad.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesUnalignedLoad{ "Fixes"sv, "bUnalignedLoad"sv, true };

	ModuleUnalignedLoad::ModuleUnalignedLoad() :
		Module("Unaligned Load", &bFixesUnalignedLoad)
	{}

	bool ModuleUnalignedLoad::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation{ REL::ID{ 44611, 2277131 }, REL::Offset{ 0x174, 0x192 } }.address();

		if (RELEX::IsRuntimeOG())
		{
			// CreateCommandBuffer (not needed in NG/AE)
			constexpr std::array offsets
			{
				0x320,
				0x339,
				0x341 + 0x1,  // rex prefix
				0x353,
			};

			const auto base = REL::Relocation{ REL::ID(768994) }.address();
			for (const auto offset : offsets)
			{
				const uint8_t value = 0x11;
				REL::WriteSafe(base + offset + 0x1, &value, sizeof(value));  // movaps -> movups
			}
		}

		// ApplySkinningToGeometry
		const uint8_t value = 0x10;
		REL::WriteSafe(target, &value, sizeof(value));

		return true;
	}

}