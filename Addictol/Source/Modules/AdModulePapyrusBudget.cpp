#include <Modules/AdModulePapyrusBudget.h>
#include <AdUtils.h>
#include <RE/S/Setting.h>

#include <xbyak/xbyak.h>

#include <algorithm>
#include <cstring>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesDynamicUpdateBudget{ "Patches"sv, "bDynamicUpdateBudget"sv, false };
	static REX::TOML::F32<> fUpdateBudgetBase{ "Additional"sv, "fUpdateBudgetBase"sv, 1.2f };
	static REX::TOML::F32<> fBudgetMaxFPS{ "Additional"sv, "fBudgetMaxFPS"sv, 144.0f };

	namespace budgetDetail
	{
		static float* g_frameTimer = nullptr;
		static RE::Setting* g_budgetSetting = nullptr;
		static float g_lastInterval = 1.0f / 60.0f;
		static float g_bmult = 0.0f;
		static float g_tMin = 0.0f;
		static float g_tMax = 0.0f;

		// Scales the Papyrus update budget by the smoothed real frame delta instead of a fixed fallback.
		static float CalculateUpdateBudget() noexcept
		{
			float interval = std::clamp(*g_frameTimer, g_tMin, g_tMax);
			if (interval <= g_lastInterval)
				g_lastInterval = interval;
			else
				g_lastInterval = std::min(g_lastInterval + interval * 0.0075f, interval);

			interval = g_lastInterval * g_bmult;
			if (g_budgetSetting)
				g_budgetSetting->SetFloat(interval);
			return interval;
		}

		struct BudgetInject : Xbyak::CodeGenerator
		{
			BudgetInject(std::uintptr_t a_retn, std::uintptr_t a_calc)
			{
				Xbyak::Label retn, calc;
				call(ptr[rip + calc]);
				movss(xmm6, xmm0);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(calc); dq(a_calc);
			}
		};

		static void InstallSite(std::uintptr_t a_site) noexcept
		{
			static constexpr std::uint8_t mov[] = { 0xF3, 0x0F, 0x10, 0x35 };
			if (std::memcmp(reinterpret_cast<const void*>(a_site), mov, sizeof(mov)) != 0)
				return;

			auto& tramp = REL::GetTrampoline();
			BudgetInject code(a_site, reinterpret_cast<std::uintptr_t>(&CalculateUpdateBudget));
			code.ready();
			const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(tramp.allocate(code)) - (a_site + 5));
			const auto* const r = reinterpret_cast<const std::uint8_t*>(&rel);
			RELEX::WriteSafe(a_site, { 0xE9, r[0], r[1], r[2], r[3] });
			RELEX::WriteSafeNop(a_site + 5, 0x3);
		}
	}

	ModulePapyrusBudget::ModulePapyrusBudget() :
		Module("Papyrus Budget", &bPatchesDynamicUpdateBudget)
	{}

	bool ModulePapyrusBudget::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePapyrusBudget::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace budgetDetail;

		const float base = std::clamp(fUpdateBudgetBase.GetValue(), 0.1f, 4.0f);
		const float fpsMax = std::clamp(fBudgetMaxFPS.GetValue(), 60.0f, 300.0f);
		g_bmult = base / (1.0f / 60.0f * 1000.0f) * 1000.0f;
		g_tMax = 1.0f / 60.0f;
		g_tMin = 1.0f / fpsMax;
		g_frameTimer = reinterpret_cast<float*>(REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x21C }.address());

		if (auto* coll = RE::INISettingCollection::GetSingleton())
			g_budgetSetting = coll->GetSetting("fUpdateBudgetMS:Papyrus"sv);

		InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID{ 759508, 2251303, 2251303 }, 0x3C }.address());
		InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID{ 1343068, 2251305, 2251305 }, 0xB4 }.address());
		InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID{ 890788, 2251306, 2251306 }, 0xB4 }.address());
		return true;
	}

	bool ModulePapyrusBudget::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePapyrusBudget::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
