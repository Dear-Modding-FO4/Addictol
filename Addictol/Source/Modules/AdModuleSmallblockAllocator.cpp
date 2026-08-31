#include <Modules/AdModuleSmallblockAllocator.h>
#include <Memory/AdAllocator.h>
#include <Core/AdUtils.h>

namespace Addictol
{


	// 0x1268
	class BSSmallBlockAllocator
	{
		BSSmallBlockAllocator(const BSSmallBlockAllocator&) = delete;
		BSSmallBlockAllocator& operator=(const BSSmallBlockAllocator&) = delete;
	public:
		struct Pool
		{
			struct Entry
			{
				void* nodes[2];
				// array
				void* data;
				uint16_t size;
				uint16_t count;
				// continue
				uint16_t sizeBlock;
				uint16_t unk;		// DEAF
			};

			Entry* node_start;
			Entry* node_end;
			uint32_t unk1[3];
			uint32_t sizeBlock;
			REX::W32::CRITICAL_SECTION criticalSection;
		};
		BSSmallBlockAllocator() = default;
		virtual ~BSSmallBlockAllocator();
	private:
		Pool pools[64];
		REX::W32::CRITICAL_SECTION criticalSection;
		char unk[0x38];
	};
	static_assert(sizeof(BSSmallBlockAllocator) == 0x1268);

	namespace BSSmallBlockAllocatorUtil
	{
		// 0x38
		class UserPoolBase
		{
			BSSmallBlockAllocator::Pool::Entry* node_start;
			BSSmallBlockAllocator::Pool::Entry* node_end;
			uint32_t count;
			uint32_t unk1;
			uint32_t pageSize;
			uint32_t sizeBlock;
			BSSmallBlockAllocator* allocator;
			uint32_t count2;

			UserPoolBase(const UserPoolBase&) = delete;
			UserPoolBase& operator=(const UserPoolBase&) = delete;
		public:
			UserPoolBase() = default;
			virtual ~UserPoolBase();

			[[nodiscard]] inline const BSSmallBlockAllocator::Pool::Entry* GetNodeBegin() const noexcept { return node_start; }
			[[nodiscard]] inline const BSSmallBlockAllocator::Pool::Entry* GetNodeEnd() const noexcept { return node_end; }
			[[nodiscard]] inline uint32_t GetPageSize() const noexcept { return pageSize; }
			[[nodiscard]] inline uint32_t GetCountBlock() const noexcept { return count; }
			[[nodiscard]] inline uint32_t GetSizeBlock() const noexcept { return sizeBlock; }

			template<typename Heap = ProxyCurrentHeap>
			[[nodiscard]] static void* Alloc([[maybe_unused]] UserPoolBase* a_this) noexcept
			{
				return Heap::GetSingleton()->aligned_malloc(a_this->sizeBlock, 0x10);
			}

			template<typename Heap = ProxyCurrentHeap>
			static void Dealloc([[maybe_unused]] UserPoolBase* a_this, void* a_ptr) noexcept
			{
				if (!a_ptr) return;
				Heap::GetSingleton()->aligned_free(a_ptr);
			}
		};
		static_assert(sizeof(UserPoolBase) == 0x38);

		template<uint32_t sizeBlock>
		class TLockingUserPool :
			public UserPoolBase
		{
			char unk2[0x10];

			TLockingUserPool(const TLockingUserPool&) = delete;
			TLockingUserPool& operator=(const TLockingUserPool&) = delete;
		public:
			TLockingUserPool() = default;
			virtual ~TLockingUserPool();
		};

		using LockingUserPool24 = TLockingUserPool<24>;
		using LockingUserPool32 = TLockingUserPool<32>;
		using LockingUserPool40 = TLockingUserPool<40>;
		using LockingUserPool48 = TLockingUserPool<48>;
		using LockingUserPool56 = TLockingUserPool<56>;
		using LockingUserPool64 = TLockingUserPool<64>;
		using LockingUserPool80 = TLockingUserPool<80>;
		using LockingUserPool88 = TLockingUserPool<88>;
	}

	ModuleSmallblockAllocator::ModuleSmallblockAllocator() :
		Module("Smallblock Allocator", &bPatchesSmallBlockAllocator)
	{}

	bool ModuleSmallblockAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto allocSite = REL::ID{ 674967, 2268154 }.address();
		const auto deallocSite = REL::ID{ 1552278, 2268155 }.address();

		// Visper ships here because it works perfectly; the selected heap routes this traffic through VMM instead so the allocator profiler can see it.
		if (bPatchesSmallBlockAllocatorUseSelectedHeap.GetValue())
		{
			VisitSelectedHeap([allocSite, deallocSite]<typename Heap>() {
				RELEX::DetourJump(allocSite, (uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Alloc<Heap>);
				RELEX::DetourJump(deallocSite, (uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Dealloc<Heap>);
			});
			REX::INFO("Smallblock Allocator: routed to the selected heap instead of Visper."sv);
			return true;
		}

		RELEX::DetourJump(allocSite, (uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Alloc<ProxyVisperHeap>);
		RELEX::DetourJump(deallocSite, (uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Dealloc<ProxyVisperHeap>);

		return true;
	}

}