#include <Modules/AdModuleAttachLightCrash.h>
#include <AdUtils.h>

#include <RE/B/BSContainer.h>
#include <RE/R/ReferenceEffect.h>
#include <RE/RTTI.h>

namespace RE
{
	class AttachLightHitEffectVisitor
	{
	public:
		static constexpr auto RTTI{ RE::RTTI::__AttachLightHitEffectVisitor };
		static constexpr auto VTABLE{ RE::VTABLE::__AttachLightHitEffectVisitor };

		void*       vtable;     // 00
		NiAVObject* newRoot;    // 08
		NiAVObject* foundNode;  // 10
		bool        selected;   // 18
	};
	static_assert(sizeof(AttachLightHitEffectVisitor) == 0x20);
}

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAttachLightCrash{ "Fixes"sv, "bAttachLightCrash"sv, true };

	namespace attachLightCrashDetail
	{
		using ForEachResult = RE::BSContainer::ForEachResult;

		struct Visit
		{
			static ForEachResult thunk(RE::AttachLightHitEffectVisitor* a_self, RE::ReferenceEffect* a_effect)
			{
				// Skipping the unguarded lookup leaves foundNode unchanged.
				if (a_effect->GetAttached() && !a_effect->GetAttachRoot())
					return a_self->foundNode ? ForEachResult::kStop : ForEachResult::kContinue;

				return func(a_self, a_effect);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleAttachLightCrash::ModuleAttachLightCrash() :
		Module("Attach Light Crash", &bFixesAttachLightCrash)
	{}

	bool ModuleAttachLightCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAttachLightCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation vtable{ RE::AttachLightHitEffectVisitor::VTABLE[0] };
		attachLightCrashDetail::Visit::func = vtable.write_vfunc(1, attachLightCrashDetail::Visit::thunk);

		return true;
	}

	bool ModuleAttachLightCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAttachLightCrash::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
