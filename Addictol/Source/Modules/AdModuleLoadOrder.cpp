#include <Modules/AdModuleLoadOrder.h>
#include <AdUtils.h>

#include <RE/T/TESFile.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesLoadOrder{ "Fixes"sv, "bLoadOrder"sv, true };

	namespace loadOrderDetail
	{
		struct SetChecked
		{
			static void thunk(RE::TESFile* a_self, bool a_checked)
			{
				if (!a_checked && a_self->IsActive())
					a_checked = true;
				
				return func(a_self, a_checked);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleLoadOrder::ModuleLoadOrder() :
		Module("Load Order", &bFixesLoadOrder)
	{}

	bool ModuleLoadOrder::DoQuery() const noexcept
	{
		return RELEX::IsRuntimeAE();
	}

	bool ModuleLoadOrder::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();
		loadOrderDetail::SetChecked::func = trampoline.write_call<5>(REL::Relocation<std::uintptr_t>{ REL::ID{ 4487533 }, REL::Offset{ 0xB8 } }.address(), loadOrderDetail::SetChecked::thunk);
		
		return true;
	}

	bool ModuleLoadOrder::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLoadOrder::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}