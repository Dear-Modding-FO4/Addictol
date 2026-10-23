#include <Modules/AdModuleScaleformAllocator.h>
#include <Memory/AdProfiledHeap.h>
#include <Memory/AdAllocator.h>
#include <Core/AdUtils.h>

#include <Scaleform/G/GFx_Resource.h>
#include <Scaleform/M/MemoryHeap.h>
#include <Scaleform/R/RefCountBase.h>
#include <Scaleform/S/SysAlloc.h>

#include <atomic>
#include <bit>
#include <intrin.h>

namespace Addictol
{
	namespace scaleformAllocatorDetail
	{
		inline constexpr uint32_t kMaxDiagnosticLogs{ 16 };
		inline constexpr int32_t kMaxValidRefCount{ 0x00FFFFFF };
		static std::atomic<uint32_t> s_diagnosticLogs{ 0 };

		[[nodiscard]] static bool ShouldLogDiagnostic() noexcept
		{
			return s_diagnosticLogs.fetch_add(1, std::memory_order_relaxed) < kMaxDiagnosticLogs;
		}

		// Free-list headers overwrite +0x00/+0x08 with 16-byte-aligned links.
		[[nodiscard]] static bool IsValidLiveRefCountObject(const char* a_kind, const void* a_this, int32_t a_refCount, const void* a_caller) noexcept
		{
			const auto addr = reinterpret_cast<uintptr_t>(a_this);
			if (!addr || (addr & 0x7) != 0)
				return false;

			const auto vtable = *reinterpret_cast<const uintptr_t*>(addr);
			const auto qword8 = *reinterpret_cast<const uintptr_t*>(addr + 0x8);
			const auto raw0 = vtable & ~uintptr_t(0xF);
			const auto raw8 = qword8 & ~uintptr_t(0xF);

			if (a_refCount <= 0 || a_refCount > kMaxValidRefCount || !vtable || (vtable & 0x7) != 0 || raw0 == addr || raw8 == addr)
			{
				if (ShouldLogDiagnostic())
				{
					REX::WARN("Scaleform Allocator: blocked invalid {}::Release on {:p} (vtable={:#x}, refCount={}) from {:p}"sv,
						a_kind, a_this, vtable, a_refCount, a_caller);
				}
				return false;
			}

			return true;
		}

		template<class T, auto GetCount>
		static void GuardedRelease(const char* a_kind, T* a_this, void* a_caller) noexcept
		{
			if (!a_this || !IsValidLiveRefCountObject(a_kind, a_this, GetCount(*a_this), a_caller))
				return;

			a_this->Release();
		}

		static void HKRefCountImplRelease(Scaleform::RefCountImpl* a_this) noexcept
		{
			GuardedRelease<Scaleform::RefCountImpl, [](const Scaleform::RefCountImpl& a_obj) noexcept { return a_obj.GetRefCount(); }>(
				"RefCountImpl", a_this, _ReturnAddress());
		}

		static void HKRefCountNTSImplRelease(Scaleform::RefCountNTSImpl* a_this) noexcept
		{
			GuardedRelease<Scaleform::RefCountNTSImpl, [](const Scaleform::RefCountNTSImpl& a_obj) noexcept { return a_obj.GetRefCount(); }>(
				"RefCountNTSImpl", a_this, _ReturnAddress());
		}

		static void HKResourceRelease(Scaleform::GFx::Resource* a_this) noexcept
		{
			GuardedRelease<Scaleform::GFx::Resource, [](const Scaleform::GFx::Resource& a_obj) noexcept { return a_obj.refCount.value; }>(
				"GFx::Resource", a_this, _ReturnAddress());
		}

		// Round a refcount-decremented free-list pointer back to 16 bytes.
		template<class Node>
		[[nodiscard]] static Node* HealAlignedPtr(Node* a_ptr) noexcept
		{
			const auto raw = reinterpret_cast<uintptr_t>(a_ptr);
			if ((raw & 0xF) == 0)
				return a_ptr;

			const auto low = (static_cast<uint32_t>(raw) + 0xFu) & ~0xFu;
			return reinterpret_cast<Node*>((raw & ~uintptr_t(0xFFFFFFFFu)) | low);
		}

		template<class Node>
		static Node* HealLink(Node*& a_slot, const char* a_label) noexcept
		{
			auto* const orig = a_slot;
			auto* const healed = HealAlignedPtr(orig);
			if (healed != orig)
			{
				a_slot = healed;
				if (ShouldLogDiagnostic())
				{
					REX::WARN("Scaleform Allocator: healed corrupted FreeBin {} {:#x} -> {:#x}"sv,
						a_label, reinterpret_cast<uintptr_t>(orig), reinterpret_cast<uintptr_t>(healed));
				}
			}
			return healed;
		}

		template<class Node>
		[[nodiscard]] static Node* HealNodeRing(Node* a_node) noexcept
		{
			if (!a_node)
				return nullptr;

			HealLink(a_node, "node");
			if (auto* const next = HealLink(a_node->pNext, "pNext"))
				HealLink(next->pPrev, "next->pPrev");
			if (auto* const prev = HealLink(a_node->pPrev, "pPrev"))
				HealLink(prev->pNext, "prev->pNext");

			return a_node;
		}

		struct TreeBinFindBest
		{
			static Scaleform::HeapPT::BinTNode* thunk(Scaleform::HeapPT::TreeBin* a_treeBin, std::size_t a_blocks) noexcept
			{
				return HealNodeRing(func(a_treeBin, a_blocks));
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TreeBinPullNode
		{
			static void thunk(Scaleform::HeapPT::TreeBin* a_treeBin, Scaleform::HeapPT::BinTNode* a_node) noexcept
			{
				func(a_treeBin, HealNodeRing(a_node));
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FreeBinPush
		{
			static void thunk(Scaleform::HeapPT::FreeBin* a_freeBin, Scaleform::HeapPT::BinLNode* a_node) noexcept
			{
				if (a_freeBin && a_node)
				{
					std::size_t blocks = a_node->shortSize;
					if (blocks >= 0x41)
						blocks = static_cast<const Scaleform::HeapPT::BinTNode*>(a_node)->size;

					if (blocks > 0 && blocks <= 0x40)
					{
						auto*& head = a_freeBin->listBin1[blocks - 1];
						if (head)
							head = HealNodeRing(head);
					}
					else if (blocks > 0x40 && blocks <= 0x80)
					{
						auto*& head = a_freeBin->listBin2[blocks - 0x41];
						if (head)
							head = HealNodeRing(head);
					}
					else if (blocks > 0x80)
					{
						auto& treeBin = a_freeBin->treeBin;
						std::uint64_t idx = blocks >> 6;
						if (idx)
						{
							const auto upper = static_cast<std::uint64_t>(std::bit_width(idx) - 1);
							idx = ((blocks >> (upper + 5)) & 1ULL) + 2ULL * upper;
						}
						if (idx < 64 && (treeBin.mask & (1ULL << idx)) != 0)
						{
							auto** slot = &treeBin.roots[idx];
							const std::uint64_t shift = (idx < 0x3F) ? (59ULL - (idx >> 1)) : 0ULL;
							for (std::uint64_t bits = blocks << shift, depth = 0; *slot && depth < 64; bits <<= 1, ++depth)
							{
								auto* const cur = HealNodeRing(*slot);
								*slot = cur;
								if (cur->size == blocks)
									break;
								slot = &cur->child[bits >> 63];
							}
						}
					}
				}

				func(a_freeBin, a_node);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void InstallRefCountHooks() noexcept
		{
			auto& trampoline = REL::GetTrampoline();
			const auto og = RELEX::IsRuntimeOG();

			const auto refCountRelease = Scaleform::ID::RefCountImpl::Release.address();
			const auto refCountNtsRelease = Scaleform::ID::RefCountNTSImpl::Release.address();
			const auto resourceRelease = Scaleform::ID::GFx::Resource::Release.address();

			const bool validRefCount = og ?
				RELEX::Validate(refCountRelease, { 0xF0, 0xFF, 0x49, 0x08, 0x75, 0x10 }) :
				RELEX::Validate(refCountRelease, { 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xC1, 0x41, 0x08 });
			const bool validRefCountNts = og ?
				RELEX::Validate(refCountNtsRelease, { 0xFF, 0x49, 0x08, 0x75, 0x0B }) :
				RELEX::Validate(refCountNtsRelease, { 0x83, 0x69, 0x08, 0x01, 0x75, 0x0B });
			const bool validResource = og ?
				RELEX::Validate(resourceRelease, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xF0, 0xFF, 0x49, 0x08 }) :
				RELEX::Validate(resourceRelease, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xB8 });

			if (!validRefCount || !validRefCountNts || !validResource)
			{
				REX::WARN("Scaleform Allocator: RefCount::Release byte validation failed; skipping release guards."sv);
				return;
			}

			trampoline.write_jmp<5>(refCountRelease, &HKRefCountImplRelease);
			trampoline.write_jmp<5>(refCountNtsRelease, &HKRefCountNTSImplRelease);
			trampoline.write_jmp<5>(resourceRelease, &HKResourceRelease);
		}

		static void InstallHeapPTHooks() noexcept
		{
			const auto og = RELEX::IsRuntimeOG();
			const auto findBestAddr = Scaleform::ID::HeapPT::TreeBin::FindBest.address();
			const auto pullNodeAddr = Scaleform::ID::HeapPT::TreeBin::PullNode.address();
			const auto pushAddr = Scaleform::ID::HeapPT::FreeBin::Push.address();

			const bool validFindBest = RELEX::Validate(findBestAddr, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 });
			const bool validPullNode = og ?
				RELEX::Validate(pullNodeAddr, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0x02, 0x4C, 0x8B, 0x52, 0x28 }) :
				RELEX::Validate(pullNodeAddr, { 0x4C, 0x8B, 0x02, 0x4C, 0x8B, 0xD9, 0x4C, 0x8B, 0x52, 0x28 });
			const bool validPush = og ?
				RELEX::Validate(pushAddr, { 0x8B, 0x42, 0x18, 0x4C, 0x8B, 0xC1, 0x48, 0x83, 0xF8, 0x41 }) :
				RELEX::Validate(pushAddr, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x42, 0x18, 0x48, 0x8B, 0xDA });

			if (!validFindBest || !validPullNode || !validPush)
			{
				REX::WARN("Scaleform Allocator: HeapPT::FreeBin byte validation failed; skipping FreeBin hooks."sv);
				return;
			}

			TreeBinFindBest::func = RELEX::DetourJump(findBestAddr, reinterpret_cast<uintptr_t>(&TreeBinFindBest::thunk));
			TreeBinPullNode::func = RELEX::DetourJump(pullNodeAddr, reinterpret_cast<uintptr_t>(&TreeBinPullNode::thunk));
			FreeBinPush::func = RELEX::DetourJump(pushAddr, reinterpret_cast<uintptr_t>(&FreeBinPush::thunk));
		}

		struct StockSysAllocMapper
		{
			inline static uint32_t PAGE_SIZE{ 0 };
			inline static uint64_t HEAP_SIZE{ 0 };

			// Bethesda passes 32-bit sizes to a 64-bit constructor.
			static void thunk(void* a_this, const void* a_mapper,
				[[maybe_unused]] uint64_t a_heapSize, [[maybe_unused]] uint64_t a_pageSize, bool a_unk) noexcept
			{
				func(a_this, a_mapper, HEAP_SIZE, PAGE_SIZE, a_unk);
			}

			static inline REL::Relocation<decltype(thunk)> func;

			[[nodiscard]] static bool Install() noexcept
			{
				const auto mapperCtor = Scaleform::ID::SysAllocMapper::Ctor.address();
				const auto getPageSize = RE::ID::BSScaleformSysMemMapper::GetPageSize.address();

				const bool validMapperCtor = RELEX::IsRuntimeOG() ?
					RELEX::Validate(mapperCtor, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 }) :
					RELEX::Validate(mapperCtor, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20 });
				const bool validGetPageSize = RELEX::Validate(getPageSize, { 0xB8, 0x00, 0x00, 0x01, 0x00, 0xC3 });
				if (!validMapperCtor || !validGetPageSize)
				{
					REX::ERROR("Scaleform Allocator: SysAllocMapper byte validation failed."sv);
					return false;
				}

				PAGE_SIZE = uAdditionalScaleformPageSize.GetValue();
				HEAP_SIZE = uAdditionalScaleformHeapSize.GetValue();
				PAGE_SIZE = std::clamp(PAGE_SIZE, static_cast<uint32_t>(64), static_cast<uint32_t>(2048));
				PAGE_SIZE = std::bit_floor(PAGE_SIZE);
				HEAP_SIZE = std::min(static_cast<uint64_t>(HEAP_SIZE), static_cast<uint64_t>(8192));
				HEAP_SIZE = (HEAP_SIZE + 7) & ~7;
				HEAP_SIZE = std::max(HEAP_SIZE, static_cast<uint64_t>(2048));

				REX::INFO("BSScaleformAllocator (Page: {} Kb, Heap: {} Mb)"sv, PAGE_SIZE, HEAP_SIZE);

				PAGE_SIZE *= 1024ul;
				HEAP_SIZE *= 1024ull * 1024ull;

				func = RELEX::DetourJump(mapperCtor, reinterpret_cast<uintptr_t>(&thunk));
				REL::WriteSafe(getPageSize + 0x1, &PAGE_SIZE, 4);

				InstallRefCountHooks();
				InstallHeapPTHooks();
				return true;
			}
		};
	}

	template<typename Heap>
	class BSScaleformAllocator final : public Scaleform::SysAlloc
	{
	public:
		[[nodiscard]] static BSScaleformAllocator* GetSingleton()
		{
			static BSScaleformAllocator singleton;
			return std::addressof(singleton);
		}

		struct Init
		{
			static void thunk(const Scaleform::MemoryHeap::HeapDesc& a_rootHeapDesc, Scaleform::SysAllocBase*)
			{
				func(a_rootHeapDesc, BSScaleformAllocator::GetSingleton());
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		[[nodiscard]] static bool Install() noexcept
		{
			const auto initAddr = Scaleform::ID::System::Init.address();
			if (!RELEX::Validate(initAddr, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20 }))
			{
				REX::ERROR("Scaleform Allocator: System::Init byte validation failed."sv);
				return false;
			}

			Init::func = RELEX::DetourJump(initAddr, reinterpret_cast<uintptr_t>(&Init::thunk));
			scaleformAllocatorDetail::InstallRefCountHooks();
			return true;
		}
	protected:
		// HeapMH::AllocPage requests 4096B with align=8; page-align for 4 KB masks.
		[[nodiscard]] static constexpr std::size_t NormalizeAlign(std::size_t a_size, std::size_t a_align) noexcept
		{
			return (a_size == 4096 && a_align < 4096) ?
				std::size_t{ 4096 } :
				std::max<std::size_t>(a_align, 16);
		}

		void* Alloc(std::size_t a_size, std::size_t a_align) override
		{
			if (a_size == 0)
				return nullptr;
			return Heap::GetSingleton()->aligned_malloc(a_size, NormalizeAlign(a_size, a_align));
		}

		void Free(void* a_block, [[maybe_unused]] std::size_t, [[maybe_unused]] std::size_t) override
		{
			Heap::GetSingleton()->aligned_free(a_block);
		}

		void* Realloc(void* a_block, [[maybe_unused]] std::size_t, std::size_t a_size, std::size_t a_align) override
		{
			return Heap::GetSingleton()->aligned_realloc(a_block, a_size, NormalizeAlign(a_size, a_align));
		}
	private:
		BSScaleformAllocator() = default;
		~BSScaleformAllocator() = default;

		BSScaleformAllocator(const BSScaleformAllocator&) = delete;
		BSScaleformAllocator(BSScaleformAllocator&&) = delete;	
		BSScaleformAllocator& operator=(const BSScaleformAllocator&) = delete;
		BSScaleformAllocator& operator=(BSScaleformAllocator&&) = delete;
	};

	ModuleScaleformAllocator::ModuleScaleformAllocator() :
		Module("Scaleform Allocator", &bPatchesScaleformAllocator)
	{}

	bool ModuleScaleformAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (GetSelectedHeapKind() == HeapKind::Stock)
			return scaleformAllocatorDetail::StockSysAllocMapper::Install();

		if (!VisitSelectedHeap([]<class Heap>() { return Heap::GetSingleton()->Ready(); }))
		{
			REX::ERROR("Scaleform Allocator: its heap failed to initialize; keeping the game's allocator."sv);
			return false;
		}
		return VisitSelectedHeap([]<class Heap>() {
			return InstallSelectedProfiledHeap<Heap, HeapProfileSite::Scaleform>([]<class Selected>() {
				return BSScaleformAllocator<Selected>::Install();
			});
		});
	}
}