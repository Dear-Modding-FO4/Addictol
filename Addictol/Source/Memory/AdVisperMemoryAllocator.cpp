#include <Core/AdAssert.h>
#include <Memory/AdVisperMemoryAllocator.h>

namespace Addictol
{
	namespace Visper
	{
		// For game Bethesda this needs
		static TMemoryManager::Heap::BlockHeader g_null_stupid_block_for_bethesda{};

		static std::array<int32_t, 8> g_mmVisperBlockSizes
		{
			32, 64, 128, 256, 512, 1024, 2048, 4096
		};

		TMemoryManager::Heap::ScopeLock::ScopeLock(const Heap* a_heap) noexcept :
			heap(const_cast<Heap*>(a_heap))
		{
			if (heap) heap->Lock();
		}

		TMemoryManager::Heap::ScopeLock::ScopeLock(const Heap& a_heap) noexcept :
			heap(const_cast<Heap*>(&a_heap))
		{
			if (heap) heap->Lock();
		}

		TMemoryManager::Heap::ScopeLock::~ScopeLock() noexcept
		{
			if (heap) heap->Unlock();
		}

		int32_t TMemoryManager::Heap::Page::IndexOf(void* a_ptr) const noexcept
		{
			auto ptr = reinterpret_cast<uintptr_t>(a_ptr);
			auto beg = reinterpret_cast<uintptr_t>(mem);
			auto end = beg + (static_cast<uintptr_t>(size) * sizeof(BlockHeader));
			if ((beg >= ptr) || (end > ptr))
				return static_cast<int32_t>((ptr - beg) / sizeof(BlockHeader));
			return -1;
		}

		void* TMemoryManager::Heap::Page::GetNormalPtr(BlockHeader* a_block) noexcept
		{
			return a_block ? reinterpret_cast<void*>
				(reinterpret_cast<uintptr_t>(a_block) + sizeof(BlockHeader)) : nullptr;
		}

		TMemoryManager::Heap::BlockHeader* TMemoryManager::Heap::Page::GetRelBlock(void* a_ptr) noexcept
		{
			auto block = a_ptr ? reinterpret_cast<BlockHeader*>
				(reinterpret_cast<uintptr_t>(a_ptr) - sizeof(BlockHeader)) : nullptr;
			if (block) return block->hash != BlockHeader::HASH ? nullptr : block;
			return nullptr;
		}

		TMemoryManager::Heap::BlockHeader* TMemoryManager::Heap::Page::CreateBlock(BlockHeader* a_block,
			size_t a_size, bool a_stdalloc) const noexcept
		{
			if (a_block)
			{
				a_block->hash = BlockHeader::HASH;
				a_block->size = static_cast<int32_t>(a_size);
				a_block->heapId.heap = GetHeapId();
				a_block->heapId.page = GetId();
				a_block->flags.winalloc = false;
				a_block->flags.stdalloc = a_stdalloc;
			}

			return a_block;
		}

		size_t TMemoryManager::Heap::Page::GetRealConsumptionMemory() const noexcept
		{
			auto total = static_cast<size_t>(size) * (static_cast<size_t>(blockSize) + sizeof(BlockHeader));
			total += static_cast<size_t>(size) * sizeof(uintptr_t);
			return total + sizeof(Page);
		}

		bool TMemoryManager::Heap::Page::Initialize(int8_t a_id, int8_t a_heapId, int32_t a_blockSize,
			int32_t a_totalNum)
		{
			if (IsInitialize() || !a_blockSize || !a_totalNum)
				return false;

			auto CalculateSize = [](size_t a_blockSize, size_t a_totalNum)
				{
					return (a_blockSize + sizeof(BlockHeader)) * (a_totalNum);
				};

			// align 16 all blocks
			size_t realBlockSize = (static_cast<size_t>(a_blockSize) + sizeof(BlockHeader) + 15) & ~15;
			a_blockSize = static_cast<int32_t>(realBlockSize - sizeof(BlockHeader));

			// Request allocation at an address nearby. 
			// Specifying MEM_RESERVE | MEM_COMMIT reserves address space and maps physical memory in one step.
			mem = REX::W32::VirtualAlloc(
				nullptr,
				CalculateSize(a_blockSize, a_totalNum),			// Allocation size (must be page aligned)
				REX::W32::MEM_RESERVE | REX::W32::MEM_COMMIT,
				REX::W32::PAGE_READWRITE						// Permissions (e.g., Read/Write)
			);

			if (!mem)
				return false;

			try
			{
				// init stack
				stack.resize(a_totalNum);

				// init parms heap
				size = a_totalNum;
				blockSize = a_blockSize;
				top = size - 1;
				heapId = a_heapId;
				pageId = a_id;

				// create blocks
				for (size_t i = 0; i < static_cast<size_t>(a_totalNum); i++)
				{
					auto block_int = reinterpret_cast<uintptr_t>(mem) + (i * realBlockSize);
					stack[i] = CreateBlock(reinterpret_cast<BlockHeader*>(block_int), a_blockSize);
				}

				return true;
			}
			catch (...)
			{
				REX::W32::VirtualFree(mem, 0, REX::W32::MEM_RELEASE);
				mem = nullptr;
			}

			return false;
		}

		bool TMemoryManager::Heap::Page::IsInitialize() const noexcept
		{
			return mem != nullptr;
		}

		void TMemoryManager::Heap::Page::Release() noexcept
		{
			if (IsInitialize())
			{
				REX::W32::VirtualFree(mem, 0, REX::W32::MEM_RELEASE);
				stack.clear();

				mem = nullptr;
				size = 0;
				blockSize = 0;
				top = -1;
				heapId = -1;
				pageId = -1;
			}
		}

		void* TMemoryManager::Heap::Page::Alloc(int32_t a_size) noexcept
		{
			if (!a_size || (a_size > blockSize))
				return nullptr;

			if (IsEmpty())
			{
				// get free block from standard allocator
				auto block = reinterpret_cast<BlockHeader*>(_aligned_malloc(blockSize + sizeof(BlockHeader), 16));
				return std::memset(GetNormalPtr(CreateBlock(block, a_size, true)), 0, blockSize);
			}

			// get free block and decrement top index
			auto block = stack[top--];
			block->size = static_cast<int32_t>(a_size);
			return std::memset(GetNormalPtr(block), 0, blockSize);
		}

		void* TMemoryManager::Heap::Page::Realloc(void* a_oldPtr, int32_t a_size) noexcept
		{
			// This function will repeat the code of others, but it is important not to call deadlock,
			// since I have abandoned storing the owner stream and incrementing.
			//
			// This function doesn't know about other heaps, so it returns nullptr if it can't.
			// I think its use in this area of the class is not appropriate.

			auto block = GetRelBlock(a_oldPtr);
			if (!block) return nullptr;				// bullshit user cases

			if (!a_size)							// user send 0? so... free
			{
				if (block->flags.stdalloc)
				{
					// without add into stack

					_aligned_free(block);
					return nullptr;
				}

				if ((block->heapId.heap != heapId) || (block->heapId.page != pageId) || (top == size - 1))
					return nullptr;					// causes leak blocks, but idk... this for paranoic

				// increment top index add block into stack
				stack[++top] = block;
				return nullptr;
			}

			if (a_size == block->size)				// user idiot
				return a_oldPtr;

			if (a_size < blockSize)					// within the block of this heap
			{
				if (block->size > a_size)
					std::memset(reinterpret_cast<uint8_t*>(a_oldPtr) + a_size, 0,
						static_cast<size_t>(block->size) - a_size);
				else
					std::memset(reinterpret_cast<uint8_t*>(a_oldPtr) + block->size, 0,
						static_cast<size_t>(a_size) - block->size);

				block->size = a_size;
				return a_oldPtr;
			}

			// need new block more large
			return nullptr;
		}

		void TMemoryManager::Heap::Page::Free(void* a_ptr) noexcept
		{
			auto block = GetRelBlock(a_ptr);
			if (!block) return;

			if (block->flags.stdalloc)
			{
				// without add into stack

				_aligned_free(block);
				return;
			}

			if ((block->heapId.heap != heapId) || (block->heapId.page != pageId) || (top == size - 1))
				return;

			stack[++top] = block;
		}

		int32_t TMemoryManager::Heap::Page::GetSize(void* a_ptr) const noexcept
		{
			auto block = GetRelBlock(a_ptr);
			return block ? block->size : 0;
		}

		size_t TMemoryManager::Heap::GetRealConsumptionMemory() const noexcept
		{
			auto total = static_cast<size_t>(size) * (static_cast<size_t>(blockSize) + sizeof(BlockHeader));
			total += static_cast<size_t>(size) * sizeof(uintptr_t);
			total += sizeof(Page);
			return (total * pages.size()) + sizeof(Heap);
		}

		bool TMemoryManager::Heap::Initialize(int8_t a_id, int32_t a_blockSize, int32_t a_totalNum)
		{
			if (IsInitialize() || !a_blockSize || !a_totalNum)
				return false;

			auto firstPage = std::make_shared<Page>();
			if (!firstPage)
				return false;

			if (!firstPage->Initialize(0, a_id, a_blockSize, a_totalNum))
			{
				firstPage.reset();
				return false;
			}

			try
			{
				// max pages num
				pages.reserve(127);
				pages.emplace_back(firstPage);

				// init parms heap
				size = a_totalNum;
				blockSize = a_blockSize;
				id = a_id;

				return true;
			}
			catch (...)
			{
				firstPage.reset();
			}

			return false;
		}

		bool TMemoryManager::Heap::IsInitialize() const noexcept
		{
			return pages.size() != 0;
		}

		void TMemoryManager::Heap::Release() noexcept
		{
			if (IsInitialize())
			{
				pages.clear();
				size = 0;
				blockSize = 0;
				id = -1;
			}
		}

		void* TMemoryManager::Heap::Alloc(int32_t a_size) noexcept
		{
			ScopeLock lock(this);

			for (auto& page : pages)
				if (!page->IsEmpty())
					return page->Alloc(a_size);

			if (pages.size() >= 127)	// limit
				return nullptr;

			// create new page
			auto newPage = std::make_shared<Page>();
			if (!newPage)
				return nullptr;

			if (!newPage->Initialize(static_cast<uint8_t>(pages.size()), id, blockSize, size))
			{
				newPage.reset();
				return nullptr;
			}

			pages.emplace_back(newPage);
			return newPage->Alloc(a_size);
		}

		void* TMemoryManager::Heap::Realloc(void* a_oldPtr, int32_t a_size) noexcept
		{
			ScopeLock lock(this);

			auto block = GetRelBlock(a_oldPtr);
			if (!block || (block->heapId.heap != id))
				return nullptr;

			return pages[block->heapId.page]->Realloc(a_oldPtr, a_size);
		}

		void TMemoryManager::Heap::Free(void* a_ptr) noexcept
		{
			ScopeLock lock(this);

			auto block = GetRelBlock(a_ptr);
			if (!block || (block->heapId.heap != id) || (block->heapId.page >= pages.size()))
				return;

			pages[block->heapId.page]->Free(a_ptr);
		}

		int32_t TMemoryManager::Heap::GetSize(void* a_ptr) const noexcept
		{
			auto block = GetRelBlock(a_ptr);
			return block ? block->size : 0;
		}

		void TMemoryManager::Heap::Lock() noexcept
		{
			// test_and_set returns the previous value. 
			// If it was true, we spin until it becomes false.
			while (locker.test_and_set(std::memory_order_acquire))
			{ /* Active waiting loop */ }
		}

		void TMemoryManager::Heap::Unlock() noexcept
		{
			// clear sets the flag back to false
			locker.clear(std::memory_order_release);
		}

		void* TMemoryManager::Heap::GetNormalPtr(BlockHeader* a_block) noexcept
		{
			return a_block ? reinterpret_cast<void*>
				(reinterpret_cast<uintptr_t>(a_block) + sizeof(BlockHeader)) : nullptr;
		}

		TMemoryManager::Heap::BlockHeader* TMemoryManager::Heap::GetRelBlock(void* a_ptr) noexcept
		{
			auto block = a_ptr ? reinterpret_cast<BlockHeader*>
				(reinterpret_cast<uintptr_t>(a_ptr) - sizeof(BlockHeader)) : nullptr;
			if (block) return block->hash != BlockHeader::HASH ? nullptr : block;
			return nullptr;
		}

		TMemoryManager::TMemoryManager()
		{
			g_null_stupid_block_for_bethesda.hash = Heap::BlockHeader::HASH;
			g_null_stupid_block_for_bethesda.heapId.heap = HEAP_STDALLOC;
			g_null_stupid_block_for_bethesda.flags.noaction = true;
		}

		int8_t TMemoryManager::GetHeapIdByBlockSize(int32_t a_blockSize) const noexcept
		{
			for (size_t i = 0; i < g_mmVisperBlockSizes.size(); i++)
				if (a_blockSize <= g_mmVisperBlockSizes[i])
					return static_cast<int8_t>(i);
			return -1;
		}

		bool TMemoryManager::HasHeapByBlockSize(int32_t a_blockSize) const noexcept
		{
			auto id = GetHeapIdByBlockSize(a_blockSize);
			return id == -1 ? false : heaps[id] != nullptr;
		}

		int8_t TMemoryManager::CreateNewHeap(int32_t a_blockSize, int32_t a_totalNum) noexcept
		{
			auto id = GetHeapIdByBlockSize(a_blockSize);
			if ((id == -1) || heaps[id]) return -1;

			heaps[id] = std::make_unique<Heap>();
			if (!heaps[id]) return -1;

			if (!heaps[id]->Initialize(id, a_blockSize, a_totalNum))
			{
				heaps[id].reset();
				return -1;
			}

			return id;
		}

		bool TMemoryManager::InitializeDefaultSettings() noexcept
		{
			auto CreateHeap = [](TMemoryManager* memmgr, int32_t a_sizeBlock, int32_t a_num) noexcept
				{
					auto num = a_num << 10;
					return memmgr->CreateNewHeap(a_sizeBlock, num) != -1;
				};

#if !AD_VISPER_REMOVE_BIG_STUFF
			for (int32_t i = 0; i < g_mmVisperBlockSizes.size(); i++)
				if (!CreateHeap(this, g_mmVisperBlockSizes[i], 256))
					return false;
#else
			for (int32_t i = 0; i < 3; i++)
				if (!CreateHeap(this, g_mmVisperBlockSizes[i], 256))
					return false;
#endif

			return true;
		}

		void* TMemoryManager::Alloc(size_t a_size) noexcept
		{
			if (!a_size)
				return &g_null_stupid_block_for_bethesda;

#if !AD_VISPER_REMOVE_BIG_STUFF
			if (a_size >= (64ull * 1024 * 1024))
			{
				// Request allocation at an address nearby. 
				// Specifying MEM_RESERVE | MEM_COMMIT reserves address space and maps physical memory in one step.
				auto block = reinterpret_cast<Heap::BlockHeader*>(REX::W32::VirtualAlloc(nullptr,
					a_size + sizeof(Heap::BlockHeader),				// Allocation size (must be page aligned)
					REX::W32::MEM_RESERVE | REX::W32::MEM_COMMIT,
					REX::W32::PAGE_READWRITE						// Permissions (e.g., Read/Write)
				));
				if (block)
				{
					block->hash = Heap::BlockHeader::HASH;
					block->size = -1;
					block->heapId.heap = HEAP_STDALLOC;
					block->heapId.page = 0;
					block->flags.stdalloc = false;
					block->flags.winalloc = true;
					block->winsize = a_size;

					otherTotalSize += a_size + sizeof(Heap::BlockHeader);

					// memory zeroed
					return Heap::Page::GetNormalPtr(block);
				}
				return nullptr;
			}

			auto id = GetHeapIdByBlockSize(static_cast<int32_t>(a_size));
			if (id == -1)
			{
				alloc_def:
				// get free block from standard allocator
				auto block = reinterpret_cast<Heap::BlockHeader*>(_aligned_malloc(a_size + sizeof(Heap::BlockHeader), 16));
				if (block)
				{
					block->hash = Heap::BlockHeader::HASH;
					block->size = static_cast<int32_t>(a_size);
					block->heapId.heap = HEAP_STDALLOC;
					block->heapId.page = 0;
					block->flags.stdalloc = true;
					block->flags.winalloc = false;

					otherTotalSize += a_size + sizeof(Heap::BlockHeader);

					return std::memset(Heap::Page::GetNormalPtr(block), 0, a_size);
				}
				return nullptr;
			}
#else
			auto id = GetHeapIdByBlockSize(static_cast<int32_t>(a_size));
			if (id == -1) return nullptr;
#endif

			if (heaps[id])
				return heaps[id]->Alloc(static_cast<int32_t>(a_size));

#if !AD_VISPER_REMOVE_BIG_STUFF
			goto alloc_def;
#else
			return nullptr;
#endif
		}

		void* TMemoryManager::Realloc(void* a_oldPtr, size_t a_size) noexcept
		{
			if (!a_oldPtr)
				return Alloc(a_size);

			auto block = Heap::Page::GetRelBlock(a_oldPtr);
			if (!block) return nullptr;				// bullshit user cases

			if (block->heapId.heap == HEAP_STDALLOC)
			{
				if (block->flags.stdalloc)
				{
					auto old_size = block->size;
					otherTotalSize -= old_size + sizeof(Heap::BlockHeader);
					auto new_block = reinterpret_cast<Heap::BlockHeader*>(
						_aligned_realloc(block, a_size + sizeof(Heap::BlockHeader), 16));
					if (new_block)
					{
						block->hash = Heap::BlockHeader::HASH;
						block->size = static_cast<int32_t>(a_size);
						block->heapId.heap = HEAP_STDALLOC;
						block->heapId.page = 0;
						block->flags.stdalloc = true;
						block->flags.winalloc = false;

						otherTotalSize += block->size + sizeof(Heap::BlockHeader);

						auto mem = Heap::Page::GetNormalPtr(new_block);
						if (static_cast<int32_t>(old_size) < a_size)
							return std::memset(mem, 0, a_size - old_size);
						return mem;
					}
					return new_block;
				}
				else if(block->flags.winalloc)
				{
					auto old_size = block->winsize;
					auto mem = Alloc(old_size);
					if (mem)
					{
						if (old_size < a_size)
							return std::memcpy(mem, a_oldPtr, old_size);
						return std::memcpy(mem, a_oldPtr, a_size);
					}
					Free(a_oldPtr);
					return nullptr;
				}

				return nullptr;
			}

			auto& heap = heaps[block->heapId.heap];
			if (!a_size)							// user send 0? so... free
			{
				heap->Free(a_oldPtr);
				return nullptr;
			}

			if (a_size == block->size)				// user idiot
				return a_oldPtr;

			if ((a_size < heap->GetBlockSize()) && 
				(GetHeapIdByBlockSize(static_cast<int32_t>(a_size)) == block->heapId.heap))
			{
				// within the block of this heap

				if (block->size > static_cast<size_t>(a_size))
					std::memset(reinterpret_cast<uint8_t*>(a_oldPtr) + a_size, 0,
						static_cast<size_t>(block->size) - a_size);
				else
					std::memset(reinterpret_cast<uint8_t*>(a_oldPtr) + block->size, 0,
						static_cast<size_t>(a_size) - block->size);

				block->size = static_cast<int32_t>(a_size);
				return a_oldPtr;
			}

			// need new block more large
			auto newBlock = Alloc(a_size);
			if (newBlock)
			{
				std::memcpy(newBlock, a_oldPtr, a_size);
				heap->Free(a_oldPtr);
				return newBlock;
			}

			return nullptr;
		}

		void TMemoryManager::Free(void* a_ptr) noexcept
		{
			auto block = Heap::Page::GetRelBlock(a_ptr);
			if (!block || block->flags.noaction) return;

#if !AD_VISPER_REMOVE_BIG_STUFF
			if (block->heapId.heap == HEAP_STDALLOC)
			{
				if (block->flags.stdalloc)
				{
					otherTotalSize -= block->size + sizeof(Heap::BlockHeader);
					_aligned_free(block);
				}
				else if (block->flags.winalloc)
				{
					otherTotalSize -= block->winsize + sizeof(Heap::BlockHeader);
					REX::W32::VirtualFree(block, 0, REX::W32::MEM_RELEASE);
				}
				return;
			}

			if ((block->heapId.heap == -1) || (block->heapId.heap >= static_cast<int32_t>(heaps.size())))
				return;
#else
			if ((block->heapId.heap == -1) || (block->heapId.heap >= 3))
				return;
#endif

			if (heaps[block->heapId.heap])
				heaps[block->heapId.heap]->Free(a_ptr);
		}

		size_t TMemoryManager::GetSize(void* a_ptr) const noexcept
		{
			auto block = Heap::Page::GetRelBlock(a_ptr);
			if (!block) return 0;

#if !AD_VISPER_REMOVE_BIG_STUFF
			if (block->heapId.heap == HEAP_STDALLOC)
			{
				if (block->flags.stdalloc)
					return block->size;
				else if (block->flags.winalloc)
					return block->winsize;
				return 0;
			}
#endif

			return block->size;
		}

		size_t TMemoryManager::GetRealSize(void* a_ptr) const noexcept
		{
			auto block = Heap::Page::GetRelBlock(a_ptr);
			if (!block) return 0;

#if !AD_VISPER_REMOVE_BIG_STUFF
			if (block->heapId.heap == HEAP_STDALLOC)
			{
				if (block->flags.stdalloc)
					return block->size + sizeof(Heap::BlockHeader);
				else if (block->flags.winalloc)
					return block->winsize + sizeof(Heap::BlockHeader);
				return 0;
			}
#endif

			if (heaps[block->heapId.heap])
				return heaps[block->heapId.heap]->GetBlockSize() + sizeof(Heap::BlockHeader);
			return 0;
		}

		size_t TMemoryManager::GetRealConsumptionMemory() const noexcept
		{
			size_t total = sizeof(TMemoryManager);

			for (auto& heap : heaps)
				if (heap)
					total += heap->GetRealConsumptionMemory();

			return total + otherTotalSize;
		}
	}
}