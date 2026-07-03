#include <Modules/AdModuleMagicEffectConditions.h>
#include <AdUtils.h>

#include <RE/A/Actor.h>
#include <RE/A/ActiveEffect.h>
#include <RE/A/AlchemyItem.h>
#include <RE/E/EffectItem.h>
#include <RE/M/MagicTarget.h>
#include <RE/S/Setting.h>
#include <RE/T/TESForm.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMagicEffectConditions{"Fixes"sv, "bMagicEffectConditions"sv, true};

	namespace magicEffectConditionsDetail
	{
		using EvaluateConditions_t = void (*)(RE::ActiveEffect *, float, bool);
		static inline EvaluateConditions_t _EvaluateConditions{nullptr};

		static float GetConditionUpdateInterval() noexcept
		{
			auto *settings = RE::GameSettingCollection::GetSingleton();
			auto *setting = settings ? settings->GetSetting("fActiveEffectConditionUpdateInterval"sv) : nullptr;
			if (!setting)
				return 1.0f;

			const float value = setting->GetFloat();
			return value > 0.001f ? value : 1.0f;
		}

		static void EvaluateConditions_Hook(RE::ActiveEffect *a_this, float a_elapsedTimeDelta, bool a_forceUpdate)
		{
			if (a_this->conditionStatus == RE::ActiveEffect::ConditionStatus::kNotAvailable)
			{
				// This effect has no conditions, so there is nothing to evaluate.
				return;
			}

			// kFireAndForget == 1 in FO4; commonlibf4 only forward-declares CastingType, so compare numerically.
			if (static_cast<std::int32_t>(a_this->spell->GetCastingType()) == 1 /*kFireAndForget*/)
			{
				if (a_this->spell->IsAlchemyItem())
				{
					auto *potion = static_cast<RE::AlchemyItem *>(a_this->spell);
					if (potion->data.addictionChance > 0.0f || potion->data.addictionItem)
					{
						// Fire-and-forget addictive chem/alcohol: let vanilla handle the addiction path.
						_EvaluateConditions(a_this, a_elapsedTimeDelta, a_forceUpdate);
						return;
					}
				}
			}

			if ((a_this->flags.all(RE::ActiveEffect::Flags::kHasConditions) || a_this->displacementSpell) &&
				a_this->target && a_this->target->GetTargetStatsObject())
			{
				// pad94 (0x94) is unused padding on all three runtimes; reuse it as an aux delta accumulator.
				float &aux = *reinterpret_cast<float *>(reinterpret_cast<std::byte *>(a_this) + 0x94);

				if (!a_forceUpdate)
				{
					if (a_this->elapsedSeconds <= 0.0f)
					{
						// Seed the accumulator with the time the effect has been active this tick.
						aux = a_elapsedTimeDelta;
						return;
					}

					if (aux > 0.0f && aux < GetConditionUpdateInterval())
					{
						aux += a_elapsedTimeDelta;
						return;
					}
				}

				const bool isTrue =
					a_this->effect->conditions.IsTrue(a_this->target->GetTargetStatsObject(), a_this->caster.get().get()) &&
					!a_this->CheckDisplacementSpellOnTarget();
				a_this->conditionStatus = isTrue ? RE::ActiveEffect::ConditionStatus::kTrue : RE::ActiveEffect::ConditionStatus::kFalse;
			}
			else
			{
				a_this->conditionStatus = RE::ActiveEffect::ConditionStatus::kNotAvailable;
			}
		}
	}

	ModuleMagicEffectConditions::ModuleMagicEffectConditions() : Module("MagicEffectConditions", &bFixesMagicEffectConditions)
	{
	}

	bool ModuleMagicEffectConditions::DoQuery() const noexcept
	{
		if (REX::W32::GetModuleHandleW(L"MGEFConditionFix.dll"))
		{
			REX::WARN("MagicEffectConditions: MGEFConditionFix.dll is present; standalone fix already active. Patch was not applied."sv);
			return false;
		}

		return true;
	}

	bool ModuleMagicEffectConditions::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		// OG 1228998 -> RVA 0xC4EE40, NG 2226003 -> 0xB00AD0, AE 2226003 -> 0xB744F0.
		REL::Relocation<std::uintptr_t> target{REL::ID{1228998, 2226003, 2226003}};

		const auto original = RELEX::DetourJump(
			target.address(),
			reinterpret_cast<std::uintptr_t>(&magicEffectConditionsDetail::EvaluateConditions_Hook));
		if (!original)
		{
			REX::WARN("MagicEffectConditions: failed to detour ActiveEffect::EvaluateConditions. Patch was not applied."sv);
			return true;
		}

		magicEffectConditionsDetail::_EvaluateConditions =
			reinterpret_cast<magicEffectConditionsDetail::EvaluateConditions_t>(original);

		return true;
	}

	bool ModuleMagicEffectConditions::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleMagicEffectConditions::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}
}
