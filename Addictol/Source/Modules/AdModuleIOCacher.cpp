#include <Modules\AdModuleIOCacher.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesIOChacher{ "Fixes", "bIOCacher", true };

	ModuleIOCacher::ModuleIOCacher() :
		Module("Module IO Cacher", &bFixesIOChacher)
	{}

	bool ModuleIOCacher::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleIOCacher::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto id = REL::ID{ 165043, 2268775 };

		RELEX::WriteSafe(REL::Relocation{ id, REL::Offset{ 0x84, 0x88 } }.get(), { 0x10 });
		RELEX::WriteSafe(REL::Relocation{ id, REL::Offset{ 0x8D, 0x91 } }.get(), { 0x50 });
		RELEX::WriteSafe(REL::Relocation{ id, REL::Offset{ 0x98, 0x9C } }.get(), { 0x10 });

		return true;
	}

	bool ModuleIOCacher::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleIOCacher::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}