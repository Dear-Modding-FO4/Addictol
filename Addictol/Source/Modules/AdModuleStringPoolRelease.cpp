#include <Modules/AdModuleStringPoolRelease.h>
#include <AdUtils.h>

#include <RE/B/BSStringPool.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesStringPoolRelease{ "Fixes"sv, "bStringPoolRelease"sv, true };

	namespace detail
	{
		using BSStringPool__Entry__ReleaseFn = decltype(RE::BSStringPool::Entry::release);
		static std::function<BSStringPool__Entry__ReleaseFn> BSStringPool__Entry__Release_orig{};

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

	bool ModuleStringPoolRelease::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleStringPoolRelease::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		detail::BSStringPool__Entry__Release_orig = REL::Relocation<detail::BSStringPool__Entry__ReleaseFn>
			{ RE::ID::BSStringPool::Entry::Release }.get();

		return true;
	}

	bool ModuleStringPoolRelease::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleStringPoolRelease::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}