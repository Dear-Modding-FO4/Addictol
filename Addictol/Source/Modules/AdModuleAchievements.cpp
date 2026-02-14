#include <Modules\AdModuleAchievements.h>
#include <AdUtils.h>
#include <AdAssert.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesAchievements{ "Patches"sv, "bAchievements"sv, true };

	struct Patch : Xbyak::CodeGenerator
	{
		Patch()
		{
			xor_(rax, rax);
			ret();
		}
	};

	ModuleAchievements::ModuleAchievements() :
		Module("Achievements", &bPatchesAchievements)
	{}

	bool ModuleAchievements::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAchievements::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Target;
		std::size_t Size;

		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE
			Target = REL::ID(2192323);
			Size = 0x6E;
		}
		else
		{
			// OG
			Target = REL::ID(1432894);
			Size = 0x73;
		}

		const auto Address = Target.address();
		REL::WriteSafeFill(Address, REL::INT3, Size);

		Patch p;
		p.ready();

		AdAssert(p.getSize() < Size);
		REL::WriteSafe(Address, std::span{ p.getCode<const std::byte*>(), p.getSize() });

		return true;
	}

	bool ModuleAchievements::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAchievements::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}