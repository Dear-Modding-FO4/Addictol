#include <Modules\AdModuleUnalignedLoad.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesUnalignedLoad{ "Fixes"sv, "bUnalignedLoad"sv, true };

	ModuleUnalignedLoad::ModuleUnalignedLoad() :
		Module("Unaligned Load", &bFixesUnalignedLoad)
	{}

	bool ModuleUnalignedLoad::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleUnalignedLoad::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target;
		std::size_t TargetOffset;

		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			Target = REL::ID(2277131);
			TargetOffset = 0x192;
		}
		else
		{
			// OG
			Target = REL::ID(44611);
			TargetOffset = 0x174;

			// CreateCommandBuffer (not needed in NG/AE)
			constexpr std::array Offsets
			{
				0x320,
				0x339,
				0x341 + 0x1,  // rex prefix
				0x353,
			};

			REL::Relocation<std::uintptr_t> Base{ REL::ID(768994) };
			for (const auto Offset : Offsets)
			{
				std::uint8_t Value = 0x11;
				REL::WriteSafe(Base.address() + Offset + 0x1, &Value, sizeof(Value));  // movaps -> movups
			}
		}

		// ApplySkinningToGeometry
		std::uint32_t Value = 0x10;
		REL::WriteSafe(Target.address() + TargetOffset, &Value, sizeof(Value));

		return true;
	}

	bool ModuleUnalignedLoad::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleUnalignedLoad::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}