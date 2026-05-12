#include <Modules/AdModuleLODSpecular.h>
#include <AdUtils.h>

#include <RE/B/BSGeometry.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiPointer.h>

namespace Addictol
{
	// Opt-in until FOLIP coexistence is resolved (Perchik's call).
	static REX::TOML::Bool<> bPatchesLODSpecular{ "Patches"sv, "bLODSpecular"sv, false };

	using TPrepare = void(__fastcall*)(void*);
	using TSetFlag = void(__fastcall*)(RE::BSShaderProperty*, std::uint32_t, bool);

	static TPrepare OriginalPrepare = nullptr;
	static TSetFlag SetShaderFlag = nullptr;

	// BGSDistantObjectBlock layout (verified OG/AE disasm): NiNode* block @ +0x8, bool bPrepared @ +0x2A.
	static constexpr std::size_t kBlockOffset = 0x8;
	static constexpr std::size_t kPreparedFlag = 0x2A;
	static constexpr std::uint32_t kSpecularBit = 0;

	static void ForceSpecularOnChildren(void* a_this) noexcept
	{
		auto* base = reinterpret_cast<std::byte*>(a_this);
		auto* block = *reinterpret_cast<RE::NiNode**>(base + kBlockOffset);
		if (!block)
			return;

		// Range-for uses NiTArray's filled-slot iterator; raw size() can miss children past unfilled slots.
		for (auto& slot : block->children)
		{
			auto* child = slot.get();
			if (!child)
				continue;

			auto* geom = child->IsGeometry();
			if (!geom)
				continue;

			auto* shade = static_cast<RE::BSShaderProperty*>(geom->properties[1].get());
			if (!shade)
				continue;

			SetShaderFlag(shade, kSpecularBit, true);
		}
	}

	static void __fastcall HookPrepare(void* a_this) noexcept
	{
		// Gate on pre-state: original short-circuits on already-prepared blocks; we should too.
		const bool wasPrepared = a_this && *(reinterpret_cast<std::byte*>(a_this) + kPreparedFlag) != std::byte{ 0 };

		if (OriginalPrepare)
			OriginalPrepare(a_this);

		if (wasPrepared)
			return;

		__try {
			ForceSpecularOnChildren(a_this);
		}
		__except (1) {
			// Swallow: child traversal must not crash the engine on malformed LOD data.
		}
	}

	ModuleLODSpecular::ModuleLODSpecular() :
		Module("LOD Specular", &bPatchesLODSpecular)
	{}

	bool ModuleLODSpecular::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLODSpecular::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		SetShaderFlag = reinterpret_cast<TSetFlag>(REL::ID{ 1251793, 2316281 }.address());
		if (!SetShaderFlag)
			return false;

		const auto target = REL::ID{ 950871, 2213394 }.address();
		*reinterpret_cast<uintptr_t*>(&OriginalPrepare) =
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&HookPrepare));
		return OriginalPrepare != nullptr;
	}

	bool ModuleLODSpecular::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLODSpecular::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
