#pragma once

#include <stdint.h>
#include <REX/REX.h>

#include <vector>
#include <memory>
#include <atomic>
#include <array>

namespace Addictol
{
	//	The idea is to create a simple memory manager based on blocks pre-aligned to 16, while no block search is performed.
	//	Free blocks are located in an array of pointers and their output works on the stack principle, if there are no free blocks,
	//	it is allocated by a standard allocator.
	//
	//	This will provide a great performance advantage, both in freeing and allocating memory blocks.

	namespace Visper
	{
		class TMemoryManager
		{
		public:
			class Heap
			{
			public:
				struct BlockHeader
				{
					// Unique hash for blocks this allocator
					constexpr static uint16_t HASH = 0xD2E1;

					struct Flags
					{
						uint8_t stdalloc : 1;	// Will used standard allocator (malloc)
						uint8_t reserved : 7;
					};

					uint16_t hash{ HASH };
					Flags flags{ 0 };			// Flags
					int8_t heapId{ -1 };		// Index of the heap to which the block belongs
					int32_t size{ 0 };			// Size that was requested
				};

				class ScopeLock
				{
					Heap* heap{ nullptr };
				private:
					ScopeLock(const ScopeLock&) = delete;
					ScopeLock(ScopeLock&&) = delete;
					ScopeLock& operator=(const ScopeLock&) = delete;
					ScopeLock& operator=(ScopeLock&&) = delete;
				public:
					ScopeLock(const Heap* a_heap) noexcept;
					ScopeLock(const Heap& a_heap) noexcept;
					~ScopeLock() noexcept;
				};
			private:
				void* mem{ nullptr };							// Heap memory
				int32_t 
					size{ 0 },									// Total num blocks
					blockSize{ 0 },								// Block size
					top{ -1 },									// Topmost of the free blocks
					id{ -1 };									// Index
				std::vector<BlockHeader*> stack{};				// Stack free blocks
				std::atomic_flag locker = ATOMIC_FLAG_INIT;		// Lock for sync

				Heap(const Heap&) = delete;
				Heap(Heap&&) = delete;
				Heap& operator=(const Heap&) = delete;
				Heap& operator=(Heap&&) = delete;

				[[nodiscard]] int32_t IndexOf(void* a_ptr) const noexcept;
				[[nodiscard]] void* GetNormalPtr(BlockHeader* a_block) const noexcept;
				[[nodiscard]] BlockHeader* GetRelBlock(void* a_ptr) const noexcept;
				[[nodiscard]] BlockHeader* CreateBlock(BlockHeader* a_block, size_t a_size,
					bool a_stdalloc = false) const noexcept;
			public:
				Heap() = default;
				~Heap() = default;

				[[nodiscard]] inline int8_t GetId() const noexcept { return static_cast<int8_t>(id); };
				[[nodiscard]] inline int32_t GetBlockCount() const noexcept { return size; };
				[[nodiscard]] inline int32_t GetBlockSize() const noexcept { return blockSize; };
				[[nodiscard]] inline int32_t GetFirstFreeBlock() const noexcept { return top; };
				[[nodiscard]] inline bool IsEmpty() const noexcept { return GetFirstFreeBlock() == -1; };
				[[nodiscard]] size_t GetRealConsumptionMemory() const noexcept;

				[[nodiscard]] bool Initialize(int8_t a_id, int32_t a_blockSize, int32_t a_totalNum);
				[[nodiscard]] bool IsInitialize() const noexcept;
				void Release() noexcept;

				[[nodiscard]] void* Alloc(int32_t a_size) noexcept;
				[[nodiscard]] void* Realloc(void* a_oldPtr, int32_t a_size) noexcept;
				void Free(void* a_ptr) noexcept;
				[[nodiscard]] int32_t GetSize(void* a_ptr) const noexcept;

				void Lock() noexcept;
				void Unlock() noexcept;
			};
		private:
			std::array<std::unique_ptr<Heap>, 8> heaps;

			TMemoryManager(const TMemoryManager&) = delete;
			TMemoryManager(TMemoryManager&&) = delete;
			TMemoryManager& operator=(const TMemoryManager&) = delete;
			TMemoryManager& operator=(TMemoryManager&&) = delete;
		public:
			TMemoryManager() = default;
			~TMemoryManager() = default;

			[[nodiscard]] int8_t GetHeapIdByBlockSize(int32_t a_blockSize) const noexcept;
			[[nodiscard]] bool HasHeapByBlockSize(int32_t a_blockSize) const noexcept;

			[[nodiscard]] int8_t CreateNewHeap(int32_t a_blockSize, int32_t a_totalNum) noexcept;
			[[nodiscard]] bool DeleteHeap(int8_t a_id) noexcept;
			[[nodiscard]] bool DeleteHeapByBlockSize(int32_t a_blockSize) noexcept;
			[[nodiscard]] int8_t IndexOf(void* lpBlock) const noexcept;

			[[nodiscard]] void* Alloc(int32_t a_size) noexcept;
			[[nodiscard]] void* Realloc(void* a_oldPtr, int32_t a_size) noexcept;
			void Free(void* a_ptr) noexcept;
			[[nodiscard]] int32_t GetSize(void* a_ptr) const noexcept;

			[[nodiscard]] size_t GetRealConsumptionMemory() const noexcept;
		};
	}
}