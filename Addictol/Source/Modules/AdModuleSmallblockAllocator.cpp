#include <Modules/AdModuleSmallblockAllocator.h>
#include <AdVisperMemoryAllocator.h>
#include <AdAllocator.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesSmallBlockAllocator{ "Patches"sv, "bSmallBlockAllocator"sv, true };
	static Visper::TMemoryManager g_SmallblockMemoryManager{};

	class ProxySmallblockHeap :
		public ICheckerPointer,
		public REX::TSingleton<ProxySmallblockHeap>
	{
		ProxySmallblockHeap(const ProxySmallblockHeap&) = delete;
		ProxySmallblockHeap(ProxySmallblockHeap&&) = delete;
		ProxySmallblockHeap& operator=(const ProxySmallblockHeap&) = delete;
		ProxySmallblockHeap& operator=(ProxySmallblockHeap&&) = delete;
	public:
		ProxySmallblockHeap() noexcept = default;
		~ProxySmallblockHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept { return aligned_malloc(nSize, 16); }
		[[nodiscard]] void* aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept
		{
			return g_SmallblockMemoryManager.Alloc(static_cast<int32_t>(nSize));
		}

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept
		{ return aligned_realloc(lpBlock, nNewSize, 16); }
		[[nodiscard]] void* aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept
		{
			return g_SmallblockMemoryManager.Realloc(lpBlock, static_cast<int32_t>(nNewSize));
		}

		void free(void* lpBlock) const noexcept { aligned_free(lpBlock); }
		void aligned_free(void* lpBlock) const noexcept
		{
			__try
			{
				g_SmallblockMemoryManager.Free(lpBlock);
			}
			__except (1)
			{}
		}

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept { return aligned_msize(lpBlock, 16); }
		[[nodiscard]] size_t aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept
		{
			__try
			{
				return static_cast<size_t>(g_SmallblockMemoryManager.GetSize(lpBlock));
			}
			__except (1)
			{
				return 0;
			}
		}
	};

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

	bool ModuleSmallblockAllocator::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleSmallblockAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (g_SmallblockMemoryManager.CreateNewHeap(32, 512 * 1024) == -1)
		{
			REX::WARN("SmallblockMemoryManager::CreateNewHeap for blocks {} return failed", 32);
			return false;
		}

		if (g_SmallblockMemoryManager.CreateNewHeap(64, 512 * 1024) == -1)
		{
			REX::WARN("SmallblockMemoryManager::CreateNewHeap for blocks {} return failed", 64);
			return false;
		}

		if (g_SmallblockMemoryManager.CreateNewHeap(96, 128 * 1024) == -1)
		{
			REX::WARN("SmallblockMemoryManager::CreateNewHeap for blocks {} return failed", 96);
			return false;
		}

		REX::INFO("SmallblockMemoryManager::ConsumptionMemory {}Mb", 
			g_SmallblockMemoryManager.GetRealConsumptionMemory() / (1024 * 1024));

		RELEX::DetourJump(REL::ID{ 674967,  2268154 }.address(),
			(uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Alloc<ProxySmallblockHeap>);
		RELEX::DetourJump(REL::ID{ 1552278, 2268155 }.address(),
			(uintptr_t)&BSSmallBlockAllocatorUtil::UserPoolBase::Dealloc<ProxySmallblockHeap>);

		return true;
	}

	bool ModuleSmallblockAllocator::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleSmallblockAllocator::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}