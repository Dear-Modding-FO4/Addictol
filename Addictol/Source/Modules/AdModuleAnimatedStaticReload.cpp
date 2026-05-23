#include <Modules/AdModuleAnimatedStaticReload.h>
#include <AdUtils.h>

#include <RE/B/BGSMovableStatic.h>
#include <RE/B/BSTArray.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiPointer.h>
#include <RE/T/TESFormUtil.h>
#include <RE/T/TESObjectREFR.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAnimatedStaticReload{ "Fixes"sv, "bAnimatedStaticReload"sv, true };

	namespace animatedStaticReloadDetail
	{
		// NiTimeController::CycleType isn't exposed by commonlibf4; mirror the NetImmerse enum order.
		enum CycleType : std::uint32_t
		{
			kLoop = 0,
			kReverse = 1,
			kClamp = 2,
		};

		static constexpr std::uint32_t kVfuncIndex = 0x9D;

		using TShouldSave = bool(__fastcall*)(const RE::TESObjectREFR*);
		static TShouldSave OriginalShouldSave = nullptr;

		using ActiveSequences = RE::BSTArray<RE::NiPointer<RE::NiControllerSequence>>;

		[[nodiscard]] static CycleType GetCycleType(const RE::NiControllerSequence* a_seq) noexcept
		{
			return *reinterpret_cast<const CycleType*>(a_seq->cycleType);
		}

		[[nodiscard]] static const ActiveSequences* GetActiveSequences(const RE::NiControllerManager* a_mgr) noexcept
		{
			if (!a_mgr)
				return nullptr;
			return reinterpret_cast<const ActiveSequences*>(a_mgr->activeSequences);
		}

		[[nodiscard]] static bool HasLoopingActiveSequence(const RE::NiAVObject* a_root) noexcept
		{
			if (!a_root)
				return false;
			auto* mgr = RE::NiControllerManager::GetNiControllerManager(a_root);
			const auto* sequences = GetActiveSequences(mgr);
			if (!sequences)
				return false;
			for (const auto& seq : *sequences) {
				const auto* raw = seq.get();
				if (!raw)
					continue;
				if (raw->state == RE::NiControllerSequence::AnimState::kAnimating &&
					GetCycleType(raw) == kLoop)
					return true;
			}
			return false;
		}

		static bool __fastcall HookShouldSaveAnimationOnSaving(const RE::TESObjectREFR* a_ref) noexcept
		{
			const bool original = OriginalShouldSave ? OriginalShouldSave(a_ref) : false;
			if (original || !a_ref)
				return original;

			__try {
				const auto* base = a_ref->GetObjectReference();
				if (!base || !base->As<RE::BGSMovableStatic>())
					return false;
				return HasLoopingActiveSequence(a_ref->Get3D());
			}
			__except (1) {
				return original;
			}
		}
	}

	ModuleAnimatedStaticReload::ModuleAnimatedStaticReload() :
		Module("Animated Static Reload", &bFixesAnimatedStaticReload)
	{}

	bool ModuleAnimatedStaticReload::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAnimatedStaticReload::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace animatedStaticReloadDetail;

		// Index 0 is the primary vtable; entries 1-7 are short sub-object vtables that have no method at slot 0x9D.
		const auto vtable = RE::VTABLE::TESObjectREFR[0].address();
		*reinterpret_cast<std::uintptr_t*>(&OriginalShouldSave) =
			RELEX::DetourVTable(vtable, reinterpret_cast<std::uintptr_t>(&HookShouldSaveAnimationOnSaving), kVfuncIndex);

		return OriginalShouldSave != nullptr;
	}

	bool ModuleAnimatedStaticReload::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAnimatedStaticReload::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
