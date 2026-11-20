#include <Modules/AdModuleLeveledItemHealth.h>
#include <Core/AdUtils.h>
#include <Core/AdAssert.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	// TESObjectREFR::InitLeveledItems resolves a leveled container entry into a BSScrapArray<CALCED_OBJECT>
	// (0x58-byte elements, 16 element initial reserve) and applies the entry's health to every resolved item
	// through ContainerItemExtra::AdjustHealthPercent(&calced[i].extra). The loop addresses the array with the
	// outer container entry index instead of its own counter, so an entry index >= the array capacity writes
	// past the allocation, and a smaller one gives the health to the wrong item.
	namespace leveledItemHealthDetail
	{
		constexpr int kElementSize = 0x58;
		constexpr int kExtraOffset = 0x18;

		// OG: the array data pointer is reloaded on every iteration, so only the index scaling changes.
		struct PatchOG : RELEX::Stub
		{
			PatchOG()
			{
				imul(ecx, ebx, kElementSize);
				add(rcx, ptr[rbp - 0x29]);
				add(rcx, kExtraOffset);
			}
		};

		// NG/AE: r15 held entryIndex * 0x58 across the loop. It is overwritten after the loop and untouched when
		// the loop is skipped, so it now holds the address of the first element's extra and ebx scales from it.
		struct PatchHoist : RELEX::Stub
		{
			PatchHoist()
			{
				mov(r15, ptr[rbp - 0x29]);
				add(r15, kExtraOffset);
			}
		};

		struct PatchLoop : RELEX::Stub
		{
			PatchLoop()
			{
				mov(rdx, ptr[rdi + 0x10]);
				movss(xmm1, ptr[rdx + 0x10]);
				imul(ecx, ebx, kElementSize);
				add(rcx, r15);
			}
		};

		void Write(std::uintptr_t a_target, std::size_t a_size, RELEX::Stub& a_patch)
		{
			a_patch.ready();
			AdAssert(a_patch.getSize() <= a_size);

			REL::WriteSafeFill(a_target, REL::NOP, a_size);
			REL::WriteSafe(a_target, std::span{ a_patch.getCode<const std::byte*>(), a_patch.getSize() });
		}
	}

	ModuleLeveledItemHealth::ModuleLeveledItemHealth() :
		Module("Leveled Item Health", &bFixesLeveledItemHealth)
	{}

	bool ModuleLeveledItemHealth::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLeveledItemHealth::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace leveledItemHealthDetail;

		const auto target = REL::Relocation{ REL::ID{ 69131, 2200959 }, REL::Offset{ 0x354, 0x394 } }.address();

		if (RELEX::IsRuntimeOG())
		{
			// mov rcx,[rbp-29h] / add rcx,18h / add rcx,rdi
			if (!RELEX::Validate(target, { 0x48, 0x8B, 0x4D, 0xD7, 0x48, 0x83, 0xC1, 0x18, 0x48, 0x03, 0xCF }))
			{
				Skip("unexpected bytes at TESObjectREFR::InitLeveledItems"sv);
				return false;
			}

			PatchOG patch;
			Write(target, 11, patch);
			return true;
		}

		// mov eax,r12d / imul r15,rax,58h, then the loop: mov rax,[rbp-29h] ... add rcx,r15
		const auto loop = target + 12;
		if (!RELEX::Validate(target + 3, { 0x4C, 0x6B, 0xF8, 0x58 }) ||
			!RELEX::Validate(loop, { 0x48, 0x8B, 0x45, 0xD7 }) ||
			!RELEX::Validate(loop + 17, { 0x49, 0x03, 0xCF }))
		{
			Skip("unexpected bytes at TESObjectREFR::InitLeveledItems"sv);
			return false;
		}

		PatchHoist hoist;
		PatchLoop body;
		Write(target, 12, hoist);
		Write(loop, 20, body);
		return true;
	}
}
