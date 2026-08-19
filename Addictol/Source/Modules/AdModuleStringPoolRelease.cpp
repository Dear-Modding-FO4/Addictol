#include <Modules/AdModuleStringPoolRelease.h>
#include <Core/AdUtils.h>

#include <RE/B/BSStringPool.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesStringPoolRelease{ "Fixes"sv, "bStringPoolRelease"sv, true };

	namespace detail
	{
		using BSStringPool__Entry__ReleaseFn = decltype(RE::BSStringPool::Entry::release)*;
		static BSStringPool__Entry__ReleaseFn BSStringPool__Entry__Release_orig{ nullptr };

		// An attempt to not shutdown the game due to a garbage Entry, although I'm sure it's caused by pool overflow.
		// So this may cause other problems.
		static void BSStringPool__Entry__Release(RE::BSStringPool::Entry*& a_entry) noexcept
		{
			__try
			{
				BSStringPool__Entry__Release_orig(a_entry);
			}
			__except(1)
			{}
		}
	}

	ModuleStringPoolRelease::ModuleStringPoolRelease() :
		Module("String Pool Release", &bFixesStringPoolRelease)
	{}

	bool ModuleStringPoolRelease::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		*(uintptr_t*)&detail::BSStringPool__Entry__Release_orig =
			RELEX::DetourJump(REL::Relocation{ RE::ID::BSStringPool::Entry::Release }.address(), 
				(uintptr_t)&detail::BSStringPool__Entry__Release);

		return true;
	}

}