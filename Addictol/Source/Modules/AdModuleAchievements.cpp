#include <Modules\AdModuleAchievements.h>
#include <AdUtils.h>
#include <AdAssert.h>

#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesAchievements{ "Patches", "bAchievements", true };

	struct Patch : Xbyak::CodeGenerator
	{
		Patch()
		{
			xor_(rax, rax);
			ret();
		}
	};

	ModuleAchievements::ModuleAchievements() :
		Module("Module Achievements", &bPathesAchievements)
	{}

	bool ModuleAchievements::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAchievements::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
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
}