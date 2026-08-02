#include <Modules/AdModuleAchievements.h>
#include <AdUtils.h>
#include <AdAssert.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesAchievements{ "Patches"sv, "bAchievements"sv, true };

	namespace achievementsDetail
	{
		struct Patch : Xbyak::CodeGenerator
		{
			Patch()
			{
				xor_(rax, rax);
				ret();
			}
		};
	}

	ModuleAchievements::ModuleAchievements() :
		Module("Achievements", &bPatchesAchievements)
	{}

	bool ModuleAchievements::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAchievements::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation{ REL::ID{ 1432894, 2192323 } }.address();
		const auto size = REL::Offset{ 0x73, 0x6E }.offset();

		if (!RELEX::Validate(target, { 0x48, 0x83, 0xEC, 0x28, 0xC6, 0x44, 0x24, 0x38, 0x00 }))
			return false;

		REL::WriteSafeFill(target, REL::INT3, size);
		
		achievementsDetail::Patch p;
		p.ready();

		AdAssert(p.getSize() < size);
		REL::WriteSafe(target, std::span{ p.getCode<const std::byte*>(), p.getSize() });

		return true;
	}

	bool ModuleAchievements::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAchievements::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}