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
		if (!RELEX::IsRuntimeOG())
		{
			auto off = REL::ID(2268775).address();

			RELEX::WriteSafe(off + 0x88, { 0x10 });
			RELEX::WriteSafe(off + 0x91, { 0x50 });
			RELEX::WriteSafe(off + 0x9C, { 0x10 });
		}
		else
		{
			auto off = REL::ID(165043).address();

			RELEX::WriteSafe(off + 0x84, { 0x10 });
			RELEX::WriteSafe(off + 0x8D, { 0x50 });
			RELEX::WriteSafe(off + 0x98, { 0x10 });
		}

		return true;
	}

	bool ModuleIOCacher::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}