#include <AdAssert.h>
#include <AdVisperMemoryAllocator.h>

namespace Addictol
{
	namespace Visper
	{
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

		void* TMemoryManager::Heap::Page::GetNormalPtr(BlockHeader* a_block) const noexcept
		{
			return a_block ? reinterpret_cast<void*>
				(reinterpret_cast<uintptr_t>(a_block) + sizeof(BlockHeader)) : nullptr;
		}

		TMemoryManager::Heap::BlockHeader* TMemoryManager::Heap::Page::GetRelBlock(void* a_ptr) const noexcept
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
				REX::W32::PAGE_EXECUTE_READWRITE				// Permissions (e.g., Read/Write/Execute)
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
			return total + sizeof(Heap);
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

		void* TMemoryManager::Heap::GetNormalPtr(BlockHeader* a_block) const noexcept
		{
			return a_block ? reinterpret_cast<void*>
				(reinterpret_cast<uintptr_t>(a_block) + sizeof(BlockHeader)) : nullptr;
		}

		TMemoryManager::Heap::BlockHeader* TMemoryManager::Heap::GetRelBlock(void* a_ptr) const noexcept
		{
			auto block = a_ptr ? reinterpret_cast<BlockHeader*>
				(reinterpret_cast<uintptr_t>(a_ptr) - sizeof(BlockHeader)) : nullptr;
			if (block) return block->hash != BlockHeader::HASH ? nullptr : block;
			return nullptr;
		}

		int8_t TMemoryManager::GetHeapIdByBlockSize(int32_t a_blockSize) const noexcept
		{
			if (a_blockSize <= 32)
				return 0;
			else if (a_blockSize <= 64)
				return 1;
			else if (a_blockSize <= 128)
				return 2;
			else if (a_blockSize <= 256)
				return 3;
			else if (a_blockSize <= 512)
				return 4;
			else if (a_blockSize <= 1024)
				return 5;
			else if (a_blockSize <= 2048)
				return 6;
			else if (a_blockSize <= 4096)
				return 7;
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

		int8_t TMemoryManager::IndexOf(void* lpBlock) const noexcept
		{
			if (!lpBlock) return -1;
			auto block = reinterpret_cast<Heap::BlockHeader*>
				(reinterpret_cast<uintptr_t>(lpBlock) - sizeof(Heap::BlockHeader));
			if (block->hash == Heap::BlockHeader::HASH)
				return block->heapId.heap;
			return -1;
		}

		void* TMemoryManager::Alloc(int32_t a_size) noexcept
		{
			if (!a_size)
				return nullptr;

			auto id = GetHeapIdByBlockSize(a_size);
			if (id == -1) return nullptr;

			if (heaps[id])
				return heaps[id]->Alloc(a_size);

			return nullptr;
		}

		void* TMemoryManager::Realloc(void* a_oldPtr, int32_t a_size) noexcept
		{
			if (!a_oldPtr)
				return Alloc(a_size);

			auto block = reinterpret_cast<Heap::BlockHeader*>
				(reinterpret_cast<uintptr_t>(a_oldPtr) - sizeof(Heap::BlockHeader));
			if (block->hash != Heap::BlockHeader::HASH)
				return nullptr;						// bullshit user cases

			auto& heap = heaps[block->heapId.heap];
			if (!a_size)							// user send 0? so... free
			{
				heap->Free(a_oldPtr);
				return nullptr;
			}

			if (a_size == block->size)				// user idiot
				return a_oldPtr;

			if ((a_size < heap->GetBlockSize()) && (GetHeapIdByBlockSize(a_size) == block->heapId.heap))
			{
				// within the block of this heap

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
			auto id = IndexOf(a_ptr);

			if ((id == -1) || (id >= static_cast<int32_t>(heaps.size())))
				return;

			if (heaps[id])
				heaps[id]->Free(a_ptr);
		}

		int32_t TMemoryManager::GetSize(void* a_ptr) const noexcept
		{
			auto id = IndexOf(a_ptr);
			if ((id == -1) || (id >= static_cast<int8_t>(heaps.size())))
				return 0;

			if (heaps[id])
				return heaps[id]->GetSize(a_ptr);
			return 0;
		}

		size_t TMemoryManager::GetRealConsumptionMemory() const noexcept
		{
			size_t total = sizeof(TMemoryManager);

			for (auto& heap : heaps)
				if (heap)
					total += heap->GetRealConsumptionMemory();

			return total;
		}
	}
}